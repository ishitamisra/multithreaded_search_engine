#pragma once
// Level 2: a multithreaded, polite, restartable web crawler.
//   - Frontier: a thread-safe FIFO queue of URLs to visit (BFS order,
//     which is our answer to "decide what makes one URL better than
//     another": prefer pages closer to the seed set).
//   - Dedup: a thread-safe seen-URL set so no URL is fetched twice.
//   - Politeness: robots.txt is fetched and honored per host, and a
//     minimum per-host delay is enforced between requests.
//   - Restartability: on every run the frontier and seen-set are
//     checkpointed to `outputDir`, so a subsequent run with a higher
//     page budget picks up where the last one left off.
//   - Output: each fetched page's raw bytes go to
//     outputDir/pages/<id>.html, and one manifest.tsv row per page
//     records id, path, url, title and outgoing links -- this is what
//     Indexer::build() and PageRank consume afterwards.
//
// Networking uses libcurl when available (ENGINE_HAVE_CURL); without it
// the crawler still compiles and runs but fetch() always fails, which is
// reported clearly rather than silently producing an empty crawl.

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/robots.h"

namespace engine {

struct CrawlerOptions {
  size_t numThreads = 4;
  size_t maxPages = 50;
  double politenessDelaySeconds = 0.5;
  std::string userAgent = "MultithreadedSearchEngineBot/1.0";
  bool obeyRobots = true;
  size_t maxOutlinksPerPage = 50;
  size_t maxFrontierSize = 100000;
  double fetchTimeoutSeconds = 10.0;
};

struct CrawlStats {
  size_t pagesFetched = 0;
  size_t pagesFailed = 0;
  size_t pagesSkippedRobots = 0;
};

class Crawler {
 public:
  explicit Crawler(CrawlerOptions options = {});

  // Loads one URL per line from `path` (blank lines and lines starting
  // with '#' are ignored).
  static std::vector<std::string> loadSeedsFile(const std::string& path);

  // Crawls starting from `seedUrls` (plus any checkpointed frontier found
  // in outputDir from a previous run), writing pages and a manifest into
  // outputDir. Safe to call again on the same outputDir to continue a
  // crawl that stopped after hitting maxPages.
  CrawlStats crawl(const std::vector<std::string>& seedUrls,
                    const std::string& outputDir);

 private:
  bool fetch(const std::string& url, std::string& outBody, long& outStatus);
  std::string hostOf(const std::string& url) const;
  RobotsRules& robotsForHost(const std::string& url);
  void politeWait(const std::string& host, double delaySeconds);
  void enqueueIfNew(const std::string& url);

  CrawlerOptions options_;

  std::mutex frontierMutex_;
  std::condition_variable frontierCv_;
  std::deque<std::string> frontier_;
  std::unordered_set<std::string> seen_;
  bool draining_ = false;
  int activeWorkers_ = 0;  // guarded by frontierMutex_

  std::mutex robotsMutex_;
  std::unordered_map<std::string, RobotsRules> robotsCache_;

  std::mutex politenessMutex_;
  std::unordered_map<std::string, double> nextAllowedEpochSeconds_;

  std::mutex manifestMutex_;
  std::atomic<uint32_t> nextDocId_{0};
  std::atomic<size_t> fetchedCount_{0};
};

}  // namespace engine
