#include "engine/pagerank.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "engine/threadpool.h"

namespace engine {

PageRank::PageRank(PageRankOptions options) : options_(options) {}

std::vector<double> PageRank::compute(
    size_t numDocs, const std::vector<std::vector<DocId>>& outlinks) const {
  if (numDocs == 0) return {};

  std::vector<uint32_t> outDegree(numDocs, 0);
  std::vector<std::vector<DocId>> inlinks(numDocs);
  for (size_t i = 0; i < numDocs; ++i) {
    for (DocId target : outlinks[i]) {
      if (target < numDocs) {
        outDegree[i]++;
        inlinks[target].push_back(static_cast<DocId>(i));
      }
    }
  }

  std::vector<double> scores(numDocs, 1.0 / numDocs);
  std::vector<double> next(numDocs, 0.0);
  const double d = options_.dampingFactor;
  const double base = (1.0 - d) / numDocs;
  const size_t numThreads = std::max<size_t>(1, options_.numThreads);

  for (size_t iter = 0; iter < options_.maxIterations; ++iter) {
    double danglingSum = 0.0;
    for (size_t i = 0; i < numDocs; ++i) {
      if (outDegree[i] == 0) danglingSum += scores[i];
    }
    double danglingShare = d * danglingSum / numDocs;

    {
      ThreadPool pool(numThreads);
      std::vector<std::future<void>> futures;
      for (size_t t = 0; t < numThreads; ++t) {
        futures.push_back(pool.submit([&, t]() {
          for (size_t j = t; j < numDocs; j += numThreads) {
            double sum = 0.0;
            for (DocId src : inlinks[j]) {
              sum += scores[src] / outDegree[src];
            }
            next[j] = base + danglingShare + d * sum;
          }
        }));
      }
      for (auto& f : futures) f.get();
    }

    double diff = 0.0;
    for (size_t i = 0; i < numDocs; ++i) diff += std::fabs(next[i] - scores[i]);
    std::swap(scores, next);
    if (diff < options_.convergenceEpsilon) break;
  }
  return scores;
}

void PageRank::writeScores(const std::string& pathPrefix,
                            const std::vector<double>& scores) {
  FILE* f = std::fopen((pathPrefix + ".pagerank").c_str(), "wb");
  if (!f) throw std::runtime_error("PageRank::writeScores: cannot open output file");
  uint32_t magic = 0x50524e4b;  // "PRNK"
  uint32_t count = static_cast<uint32_t>(scores.size());
  std::fwrite(&magic, sizeof(magic), 1, f);
  std::fwrite(&count, sizeof(count), 1, f);
  for (size_t i = 0; i < scores.size(); ++i) {
    uint32_t id = static_cast<uint32_t>(i);
    double score = scores[i];
    std::fwrite(&id, sizeof(id), 1, f);
    std::fwrite(&score, sizeof(score), 1, f);
  }
  std::fclose(f);
}

bool PageRank::loadScores(const std::string& pathPrefix,
                           std::vector<double>& outScores) {
  FILE* f = std::fopen((pathPrefix + ".pagerank").c_str(), "rb");
  if (!f) return false;
  uint32_t magic = 0, count = 0;
  if (std::fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x50524e4b) {
    std::fclose(f);
    return false;
  }
  if (std::fread(&count, sizeof(count), 1, f) != 1) {
    std::fclose(f);
    return false;
  }
  outScores.assign(count, 0.0);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id = 0;
    double score = 0;
    if (std::fread(&id, sizeof(id), 1, f) != 1 ||
        std::fread(&score, sizeof(score), 1, f) != 1) {
      break;
    }
    if (id < outScores.size()) outScores[id] = score;
  }
  std::fclose(f);
  return true;
}

}  // namespace engine
