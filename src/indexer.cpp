#include "engine/indexer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "engine/html_parser.h"
#include "engine/threadpool.h"
#include "engine/tokenizer.h"

namespace fs = std::filesystem;

namespace engine {

namespace {

struct ManifestEntry {
  std::string url;
  std::string title;
  std::vector<std::string> outlinkUrls;
};

// Optional crawler-produced manifest.tsv: "# search-engine crawl manifest v1"
// header followed by id\trelFilename\turl\ttitle\toutlinkUrls(csv) rows.
// Outlinks are recorded by URL rather than filename because the crawler
// discovers a link's target URL long before (if ever) it fetches and
// names that page, so filename identity isn't available yet when a row
// is written. See Crawler::writeManifest() for the writer side.
std::unordered_map<std::string, ManifestEntry> readManifestIfPresent(
    const std::string& directory) {
  std::unordered_map<std::string, ManifestEntry> byFile;
  fs::path manifestPath = fs::path(directory) / "manifest.tsv";
  std::ifstream in(manifestPath);
  if (!in) return byFile;

  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (first) {
      first = false;
      if (!line.empty() && line[0] == '#') continue;
    }
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '\t')) fields.push_back(field);
    if (fields.size() < 5) continue;
    ManifestEntry entry;
    const std::string& relFile = fields[1];
    entry.url = fields[2];
    entry.title = fields[3];
    std::stringstream outs(fields[4]);
    std::string outUrl;
    while (std::getline(outs, outUrl, ',')) {
      if (!outUrl.empty()) entry.outlinkUrls.push_back(outUrl);
    }
    byFile[relFile] = std::move(entry);
  }
  return byFile;
}

bool hasHtmlExtension(const fs::path& p,
                      const std::vector<std::string>& htmlExts) {
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return std::find(htmlExts.begin(), htmlExts.end(), ext) != htmlExts.end();
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

Indexer::Indexer(IndexerOptions options) : options_(std::move(options)) {}

void Indexer::build(const std::string& directory, InMemoryIndex& postings,
                     std::vector<DocumentMeta>& documents) {
  if (!fs::exists(directory)) {
    throw std::runtime_error("Indexer::build: directory does not exist: " +
                              directory);
  }

  // Crawler bookkeeping files (see Crawler::crawl() in crawler.cpp) live
  // alongside the fetched pages in the same output directory and must not
  // be indexed as if they were documents.
  static const std::unordered_set<std::string> kCrawlerMetadataFiles = {
      "manifest.tsv", "frontier.txt", "seen.txt"};

  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    if (kCrawlerMetadataFiles.count(entry.path().filename().string())) continue;
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  auto manifest = readManifestIfPresent(directory);

  documents.assign(files.size(), DocumentMeta{});
  for (size_t i = 0; i < files.size(); ++i) {
    DocumentMeta& doc = documents[i];
    doc.id = static_cast<DocId>(i);
    doc.path = files[i].string();
  }

  Tokenizer tokenizer(
      TokenizerOptions{options_.removeStopWords, options_.applyStemming, 1});

  const size_t numThreads = std::max<size_t>(1, options_.numThreads);
  std::vector<InMemoryIndex> partials(numThreads);
  {
    ThreadPool pool(numThreads);
    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
      futures.push_back(pool.submit([&, t]() {
        InMemoryIndex& local = partials[t];
        for (size_t i = t; i < files.size(); i += numThreads) {
          DocumentMeta& doc = documents[i];
          std::string relFile = fs::relative(files[i], directory).string();
          std::string raw = readFile(files[i]);

          std::string bodyText;
          auto manIt = manifest.find(relFile);
          bool isHtml = hasHtmlExtension(files[i], options_.htmlExtensions);
          if (isHtml) {
            ParsedHtml parsed = parseHtml(raw, manIt != manifest.end()
                                                    ? manIt->second.url
                                                    : "");
            bodyText = parsed.text;
            doc.title = parsed.title;
          } else {
            bodyText = raw;
          }

          if (manIt != manifest.end()) {
            doc.url = manIt->second.url;
            if (!manIt->second.title.empty()) doc.title = manIt->second.title;
          } else {
            doc.url = "file://" + files[i].string();
          }
          if (doc.title.empty()) doc.title = files[i].filename().string();

          std::vector<std::string> tokens = tokenizer.tokenize(bodyText);
          doc.length = static_cast<uint32_t>(tokens.size());

          std::unordered_map<std::string, Posting> perDoc;
          for (uint32_t pos = 0; pos < tokens.size(); ++pos) {
            Posting& posting = perDoc[tokens[pos]];
            posting.docId = doc.id;
            posting.termFreq++;
            posting.positions.push_back(pos);
          }
          for (auto& [word, posting] : perDoc) {
            local.getOrInsert(word).push_back(std::move(posting));
          }
        }
      }));
    }
    for (auto& f : futures) f.get();
  }

  // Resolve outlinks (filename references) to DocIds now that every file
  // has a stable id, then merge partial per-thread indexes into `postings`.
  std::unordered_map<std::string, DocId> idByUrl;
  for (size_t i = 0; i < files.size(); ++i) {
    std::string relFile = fs::relative(files[i], directory).string();
    auto manIt = manifest.find(relFile);
    if (manIt != manifest.end() && !manIt->second.url.empty()) {
      idByUrl[manIt->second.url] = documents[i].id;
    }
  }
  for (size_t i = 0; i < files.size(); ++i) {
    std::string relFile = fs::relative(files[i], directory).string();
    auto manIt = manifest.find(relFile);
    if (manIt == manifest.end()) continue;
    for (const auto& outUrl : manIt->second.outlinkUrls) {
      auto idIt = idByUrl.find(outUrl);
      if (idIt != idByUrl.end()) documents[i].outlinks.push_back(idIt->second);
    }
  }

  for (const InMemoryIndex& local : partials) {
    local.forEach([&](const std::string& word, const PostingsList& list) {
      PostingsList& merged = postings.getOrInsert(word);
      merged.insert(merged.end(), list.begin(), list.end());
    });
  }
  // Postings within a word must be sorted by DocId for merge-join query
  // execution (stream_reader.h) and for IndexWriter's offset bookkeeping.
  postings.forEachMutable([](const std::string&, PostingsList& list) {
    std::sort(list.begin(), list.end(),
              [](const Posting& a, const Posting& b) {
                return a.docId < b.docId;
              });
  });
}

}  // namespace engine
