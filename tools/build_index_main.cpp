// CLI: build_index <docs_dir> <index_prefix> [--threads N] [--stopwords] [--stem]
//
// Walks a directory of local documents (plain text, or HTML with a
// crawler-produced manifest.tsv alongside it) and writes a search index
// at <index_prefix>.{docs,lex,post}. This is the Level 1/3 pipeline:
// parse -> tokenize -> hash table -> on-disk index.

#include <chrono>
#include <iostream>

#include "engine/index_writer.h"
#include "engine/indexer.h"

using namespace engine;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0]
              << " <docs_dir> <index_prefix> [--threads N] [--stopwords] [--stem]\n";
    return 1;
  }
  std::string docsDir = argv[1];
  std::string indexPrefix = argv[2];

  IndexerOptions options;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--threads" && i + 1 < argc) {
      options.numThreads = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--stopwords") {
      options.removeStopWords = true;
    } else if (arg == "--stem") {
      options.applyStemming = true;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 1;
    }
  }

  try {
    auto start = std::chrono::steady_clock::now();

    Indexer indexer(options);
    InMemoryIndex postings;
    std::vector<DocumentMeta> documents;
    indexer.build(docsDir, postings, documents);

    IndexWriter::write(indexPrefix, postings, documents);

    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Indexed " << documents.size() << " documents, " << postings.size()
              << " distinct terms, using " << options.numThreads << " threads in "
              << elapsed << "s\n";
    std::cout << "Wrote " << indexPrefix << ".{docs,lex,post}\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
