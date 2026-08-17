// CLI: query <index_prefix> [query text...]
//
// With extra arguments, runs a single query and exits. Otherwise starts
// an interactive REPL (Level 4.1: "a simple command line interface").
// Supports the full Level 5 query language: AND (implicit), OR, NOT/-,
// "phrase matches", and parenthesized grouping.

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "engine/html_parser.h"
#include "engine/index_reader.h"
#include "engine/query_parser.h"
#include "engine/ranker.h"
#include "engine/snippet.h"

using namespace engine;

namespace {

std::string readFileToString(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool looksLikeHtml(const std::string& path) {
  return path.size() >= 5 &&
         (path.compare(path.size() - 5, 5, ".html") == 0 ||
          (path.size() >= 4 && path.compare(path.size() - 4, 4, ".htm") == 0));
}

void runQuery(const IndexReader& reader, const std::string& queryText) {
  QueryParser parser(reader);
  QueryParser::ParsedQuery parsed;
  try {
    parsed = parser.parse(queryText);
  } catch (const QueryParseError& e) {
    std::cout << "  parse error: " << e.what() << "\n";
    return;
  }

  std::vector<DocId> matches;
  for (auto& stream = parsed.stream; stream->valid(); stream->next()) {
    matches.push_back(stream->docId());
  }

  Ranker ranker(reader);
  auto results = ranker.rank(parsed.terms, matches, 10);

  std::cout << "  " << matches.size() << " match(es), showing top " << results.size() << ":\n\n";
  SnippetGenerator snippetGen;
  int rank = 1;
  for (const auto& r : results) {
    const DocumentMeta* doc = reader.getDocument(r.docId);
    if (!doc) continue;
    std::cout << "  " << rank++ << ". " << (doc->title.empty() ? "(untitled)" : doc->title)
              << "  [score " << r.score << "]\n";
    std::cout << "     " << doc->url << "\n";

    std::string raw = readFileToString(doc->path);
    std::string bodyText = looksLikeHtml(doc->path) ? parseHtml(raw, doc->url).text : raw;
    std::cout << "     " << snippetGen.generate(bodyText, parsed.terms) << "\n\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <index_prefix> [query text...]\n";
    return 1;
  }
  std::string prefix = argv[1];

  std::unique_ptr<IndexReader> reader;
  try {
    reader = std::make_unique<IndexReader>(prefix);
  } catch (const std::exception& e) {
    std::cerr << "error loading index '" << prefix << "': " << e.what() << "\n";
    return 1;
  }
  std::cout << "Loaded index: " << reader->numDocuments() << " documents\n";

  if (argc > 2) {
    std::ostringstream ss;
    for (int i = 2; i < argc; ++i) {
      if (i > 2) ss << " ";
      ss << argv[i];
    }
    runQuery(*reader, ss.str());
    return 0;
  }

  std::cout << "Enter a query (AND/OR/NOT, \"phrases\", (parens)). Empty line or Ctrl-D to quit.\n";
  std::string line;
  for (;;) {
    std::cout << "search> ";
    if (!std::getline(std::cin, line)) break;
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) break;
    runQuery(*reader, line);
  }
  return 0;
}
