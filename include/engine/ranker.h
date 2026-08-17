#pragma once
// Level 6: combine several relevance signals into a single ranking score.
//   1. Bag-of-words TF-IDF: term frequency (normalized by document
//      length) times inverse document frequency, summed over query terms.
//   2. A static, query-independent page quality score (Level 6.2) --
//      populated from PageRank (see pagerank.h) if it was computed.
//   3. Heuristic "quality of match" bonuses (Level 6.3): query terms that
//      appear in the document's title or URL are considered stronger
//      signals than a term buried in body text.

#include <string>
#include <vector>

#include "engine/common.h"
#include "engine/index_reader.h"
#include "engine/tokenizer.h"

namespace engine {

struct RankedResult {
  DocId docId;
  double score;
  double tfidfScore;
  double titleBonus;
  double urlBonus;
};

// Free struct rather than nested inside Ranker: see the comment on
// PageRankOptions in pagerank.h for why (a GCC quirk with `= {}`
// defaults on structs nested in the class being defined).
struct RankerWeights {
  double tfidf = 1.0;
  double staticScore = 2.0;
  double titleBonus = 1.5;
  double urlBonus = 0.5;
};

class Ranker {
 public:
  explicit Ranker(const IndexReader& reader, RankerWeights weights = {},
                   TokenizerOptions tokenizerOptions = {});

  // `matchedDocIds` need not be sorted or deduplicated. Returns the top
  // `topK` results, highest score first.
  std::vector<RankedResult> rank(const std::vector<std::string>& queryTerms,
                                  std::vector<DocId> matchedDocIds,
                                  size_t topK) const;

 private:
  const IndexReader& reader_;
  RankerWeights weights_;
  Tokenizer tokenizer_;
};

}  // namespace engine
