#include "engine/ranker.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace engine {

Ranker::Ranker(const IndexReader& reader, RankerWeights weights,
               TokenizerOptions tokenizerOptions)
    : reader_(reader), weights_(weights), tokenizer_(tokenizerOptions) {}

namespace {
std::unordered_set<std::string> tokenSet(const Tokenizer& tokenizer,
                                          const std::string& text) {
  auto tokens = tokenizer.tokenize(text);
  return std::unordered_set<std::string>(tokens.begin(), tokens.end());
}
}  // namespace

std::vector<RankedResult> Ranker::rank(const std::vector<std::string>& queryTerms,
                                        std::vector<DocId> matchedDocIds,
                                        size_t topK) const {
  std::sort(matchedDocIds.begin(), matchedDocIds.end());
  matchedDocIds.erase(std::unique(matchedDocIds.begin(), matchedDocIds.end()),
                       matchedDocIds.end());
  if (matchedDocIds.empty()) return {};

  std::unordered_map<DocId, double> tfidf;
  for (DocId id : matchedDocIds) tfidf[id] = 0.0;

  std::unordered_map<std::string, int> termCounts;
  for (const auto& t : queryTerms) {
    if (!t.empty()) ++termCounts[t];
  }

  const double N = std::max<size_t>(1, reader_.numDocuments());
  for (const auto& [term, count] : termCounts) {
    uint32_t df = reader_.documentFrequency(term);
    if (df == 0) continue;
    double idf = std::log(N / static_cast<double>(df)) + 1e-9;
    auto postings = reader_.openPostings(term);
    if (!postings) continue;
    for (DocId id : matchedDocIds) {
      postings->seek(id);
      if (postings->valid() && postings->docId() == id) {
        const DocumentMeta* doc = reader_.getDocument(id);
        uint32_t length = doc ? std::max<uint32_t>(1, doc->length) : 1;
        double tf = static_cast<double>(postings->termFreq()) / length;
        tfidf[id] += count * idf * tf;
      }
    }
  }

  std::vector<RankedResult> results;
  results.reserve(matchedDocIds.size());
  for (DocId id : matchedDocIds) {
    const DocumentMeta* doc = reader_.getDocument(id);
    RankedResult r{id, 0.0, tfidf[id], 0.0, 0.0};
    if (doc) {
      auto titleTokens = tokenSet(tokenizer_, doc->title);
      std::string lowerUrl = doc->url;
      std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(),
                      [](unsigned char c) { return std::tolower(c); });
      for (const auto& [term, count] : termCounts) {
        if (titleTokens.count(term)) r.titleBonus += count;
        if (!term.empty() && lowerUrl.find(term) != std::string::npos) {
          r.urlBonus += count;
        }
      }
      r.score = weights_.tfidf * r.tfidfScore +
                weights_.staticScore * doc->staticScore +
                weights_.titleBonus * r.titleBonus +
                weights_.urlBonus * r.urlBonus;
    } else {
      r.score = weights_.tfidf * r.tfidfScore;
    }
    results.push_back(r);
  }

  std::sort(results.begin(), results.end(),
            [](const RankedResult& a, const RankedResult& b) {
              return a.score > b.score;
            });
  if (results.size() > topK) results.resize(topK);
  return results;
}

}  // namespace engine
