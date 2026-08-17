// CLI: pagerank_tool <index_prefix> [--damping D] [--iterations N] [--threads N]
//
// Level 7.5: computes PageRank over the link graph baked into an already
// -built index (DocumentMeta::outlinks, populated by Indexer::build()
// from a crawler manifest) and writes <index_prefix>.pagerank. IndexReader
// picks this file up automatically on the next load, feeding into Ranker's
// static-score term.

#include <iostream>

#include "engine/index_reader.h"
#include "engine/pagerank.h"

using namespace engine;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0]
              << " <index_prefix> [--damping D] [--iterations N] [--threads N]\n";
    return 1;
  }
  std::string prefix = argv[1];
  PageRankOptions options;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--damping" && i + 1 < argc) {
      options.dampingFactor = std::stod(argv[++i]);
    } else if (arg == "--iterations" && i + 1 < argc) {
      options.maxIterations = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--threads" && i + 1 < argc) {
      options.numThreads = static_cast<size_t>(std::stoul(argv[++i]));
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 1;
    }
  }

  try {
    IndexReader reader(prefix);
    std::vector<std::vector<DocId>> outlinks(reader.numDocuments());
    for (const auto& doc : reader.documents()) outlinks[doc.id] = doc.outlinks;

    PageRank pagerank(options);
    auto scores = pagerank.compute(reader.numDocuments(), outlinks);
    PageRank::writeScores(prefix, scores);

    std::cout << "Computed PageRank for " << scores.size() << " documents, wrote "
              << prefix << ".pagerank\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
