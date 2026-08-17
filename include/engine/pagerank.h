#pragma once
// Level 7.5: PageRank over the link graph discovered while crawling
// (DocumentMeta::outlinks, resolved to DocIds by Indexer::build()).
// Computed via parallel power iteration: each iteration's new scores
// depend only on the previous iteration's (read-only) scores, so the
// per-document update splits cleanly across a thread pool.

#include <cstddef>
#include <string>
#include <vector>

#include "engine/common.h"

namespace engine {

// Deliberately a free struct rather than nested inside PageRank: GCC
// rejects `= {}` default arguments whose type is a struct nested in the
// very class being defined (an incomplete-class-context quirk), so every
// *Options-style parameter struct in this codebase is top-level.
struct PageRankOptions {
  size_t numThreads = 4;
  size_t maxIterations = 50;
  double dampingFactor = 0.85;
  double convergenceEpsilon = 1e-6;
};

class PageRank {
 public:
  explicit PageRank(PageRankOptions options = {});

  // outlinks[i] lists the DocIds document i links to. Returns one score
  // per document, in DocId order. Dangling nodes (no outlinks) distribute
  // their score mass evenly across every other document, as the classic
  // formulation requires to keep total mass conserved.
  std::vector<double> compute(size_t numDocs,
                               const std::vector<std::vector<DocId>>& outlinks) const;

  // Sidecar file (pathPrefix + ".pagerank") so scores can be attached to
  // an already-built index without rewriting foo.docs. IndexReader loads
  // this automatically if present.
  static void writeScores(const std::string& pathPrefix,
                           const std::vector<double>& scores);
  static bool loadScores(const std::string& pathPrefix,
                          std::vector<double>& outScores);

 private:
  PageRankOptions options_;
};

}  // namespace engine
