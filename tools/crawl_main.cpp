// CLI: crawl <seeds_file> <output_dir> [--max-pages N] [--threads N] [--delay SECONDS]
//
// Runs the Level 2 multithreaded crawler. Re-running with the same
// output_dir continues a previous crawl (frontier/seen state is
// checkpointed there); afterwards, run build_index on <output_dir>/pages
// to index the crawled pages.

#include <iostream>

#include "engine/crawler.h"

using namespace engine;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0]
              << " <seeds_file> <output_dir> [--max-pages N] [--threads N] [--delay SECONDS]\n";
#ifndef ENGINE_HAVE_CURL
    std::cerr << "note: built without libcurl -- network fetches will always fail. "
                 "Install libcurl4-openssl-dev and rebuild to crawl for real.\n";
#endif
    return 1;
  }
  std::string seedsFile = argv[1];
  std::string outputDir = argv[2];

  CrawlerOptions options;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--max-pages" && i + 1 < argc) {
      options.maxPages = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--threads" && i + 1 < argc) {
      options.numThreads = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--delay" && i + 1 < argc) {
      options.politenessDelaySeconds = std::stod(argv[++i]);
    } else if (arg == "--no-robots") {
      options.obeyRobots = false;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 1;
    }
  }

  auto seeds = Crawler::loadSeedsFile(seedsFile);
  if (seeds.empty()) {
    std::cerr << "no seed URLs found in " << seedsFile << " (and no checkpointed "
                 "frontier in " << outputDir << ")\n";
  }

  Crawler crawler(options);
  CrawlStats stats = crawler.crawl(seeds, outputDir);

  std::cout << "Fetched: " << stats.pagesFetched << ", Failed: " << stats.pagesFailed
            << ", Skipped (robots.txt): " << stats.pagesSkippedRobots << "\n";
  std::cout << "Pages and manifest written to " << outputDir << "\n";
  return 0;
}
