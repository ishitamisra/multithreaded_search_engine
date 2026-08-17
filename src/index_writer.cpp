#include "engine/index_writer.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "engine/index_format.h"

namespace engine {

namespace {
FILE* openOrThrow(const std::string& path, const char* mode) {
  FILE* f = std::fopen(path.c_str(), mode);
  if (!f) throw std::runtime_error("index_writer: cannot open " + path);
  return f;
}
}  // namespace

void IndexWriter::write(const std::string& pathPrefix,
                         const InMemoryIndex& postings,
                         const std::vector<DocumentMeta>& documents) {
  // --- documents file ---
  {
    FILE* f = openOrThrow(pathPrefix + ".docs", "wb");
    writeU32(f, kDocsMagic);
    writeU32(f, kIndexFormatVersion);
    writeU32(f, static_cast<uint32_t>(documents.size()));
    for (const auto& doc : documents) {
      writeU32(f, doc.id);
      writeString(f, doc.url);
      writeString(f, doc.path);
      writeString(f, doc.title);
      writeU32(f, doc.length);
      writeDouble(f, doc.staticScore);
      writeU32(f, static_cast<uint32_t>(doc.outlinks.size()));
      for (DocId out : doc.outlinks) writeU32(f, out);
    }
    std::fclose(f);
  }

  // Collect (word, postings*) pairs so the lexicon can be written in a
  // fixed, sorted order -- makes the on-disk lexicon binary-searchable
  // and diffs between index rebuilds meaningful.
  std::vector<std::pair<std::string, const PostingsList*>> entries;
  postings.forEach([&](const std::string& word, const PostingsList& list) {
    entries.emplace_back(word, &list);
  });
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  // --- postings file (written first so we know each word's byte offset) ---
  std::vector<uint64_t> offsets(entries.size());
  {
    FILE* f = openOrThrow(pathPrefix + ".post", "wb");
    writeU32(f, kPostMagic);
    writeU32(f, kIndexFormatVersion);
    for (size_t i = 0; i < entries.size(); ++i) {
      offsets[i] = static_cast<uint64_t>(std::ftell(f));
      for (const Posting& p : *entries[i].second) {
        writeU32(f, p.docId);
        writeU32(f, p.termFreq);
        for (uint32_t pos : p.positions) writeU32(f, pos);
      }
    }
    std::fclose(f);
  }

  // --- lexicon file ---
  {
    FILE* f = openOrThrow(pathPrefix + ".lex", "wb");
    writeU32(f, kLexMagic);
    writeU32(f, kIndexFormatVersion);
    writeU32(f, static_cast<uint32_t>(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
      writeString(f, entries[i].first);
      writeU64(f, offsets[i]);
      writeU32(f, static_cast<uint32_t>(entries[i].second->size()));
    }
    std::fclose(f);
  }
}

}  // namespace engine
