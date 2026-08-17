#include "engine/crawler.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

#include "engine/html_parser.h"

#ifdef ENGINE_HAVE_CURL
#include <curl/curl.h>
#endif

namespace fs = std::filesystem;

namespace engine {

namespace {

size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  size_t total = size * nmemb;
  static_cast<std::string*>(userdata)->append(ptr, total);
  return total;
}

std::string sanitizeForTsv(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back((c == '\t' || c == '\n' || c == '\r') ? ' ' : c);
  return out;
}

std::string zeroPadded(uint32_t id, int width = 8) {
  std::ostringstream ss;
  ss << std::setw(width) << std::setfill('0') << id;
  return ss.str();
}

double nowEpochSeconds() {
  return std::chrono::duration<double>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string urlPath(const std::string& url) {
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return "/";
  size_t pathStart = url.find('/', schemeEnd + 3);
  return pathStart == std::string::npos ? "/" : url.substr(pathStart);
}

}  // namespace

Crawler::Crawler(CrawlerOptions options) : options_(options) {}

std::vector<std::string> Crawler::loadSeedsFile(const std::string& path) {
  std::vector<std::string> seeds;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    size_t end = line.find_last_not_of(" \t\r\n");
    line = line.substr(start, end - start + 1);
    if (line.empty() || line[0] == '#') continue;
    seeds.push_back(line);
  }
  return seeds;
}

std::string Crawler::hostOf(const std::string& url) const {
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return "";
  size_t hostStart = schemeEnd + 3;
  size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url.substr(hostStart)
                                         : url.substr(hostStart, pathStart - hostStart);
}

bool Crawler::fetch(const std::string& url, std::string& outBody, long& outStatus) {
#ifdef ENGINE_HAVE_CURL
  CURL* curl = curl_easy_init();
  if (!curl) return false;
  outBody.clear();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBody);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                    static_cast<long>(options_.fetchTimeoutSeconds));
  curl_easy_setopt(curl, CURLOPT_USERAGENT, options_.userAgent.c_str());
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  CURLcode res = curl_easy_perform(curl);
  bool ok = (res == CURLE_OK);
  if (ok) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &outStatus);
  curl_easy_cleanup(curl);
  return ok;
#else
  (void)url;
  (void)outBody;
  (void)outStatus;
  return false;
#endif
}

RobotsRules& Crawler::robotsForHost(const std::string& url) {
  std::string host = hostOf(url);
  {
    std::lock_guard<std::mutex> lock(robotsMutex_);
    auto it = robotsCache_.find(host);
    if (it != robotsCache_.end()) return it->second;
  }
  std::string scheme = url.substr(0, url.find("://"));
  std::string robotsUrl = scheme + "://" + host + "/robots.txt";
  std::string body;
  long status = 0;
  RobotsRules rules;
  if (fetch(robotsUrl, body, status) && status >= 200 && status < 300) {
    rules = RobotsRules::parse(body, options_.userAgent);
  }
  std::lock_guard<std::mutex> lock(robotsMutex_);
  return robotsCache_.emplace(host, std::move(rules)).first->second;
}

void Crawler::politeWait(const std::string& host, double delay) {
  // Reserves this host's next available time slot while holding the lock,
  // so concurrent workers targeting the same host serialize onto distinct
  // slots instead of racing to read-then-write nextAllowedEpochSeconds_.
  double waitSeconds = 0;
  {
    std::lock_guard<std::mutex> lock(politenessMutex_);
    double now = nowEpochSeconds();
    auto it = nextAllowedEpochSeconds_.find(host);
    double nextAllowed = (it == nextAllowedEpochSeconds_.end()) ? now : it->second;
    waitSeconds = std::max(0.0, nextAllowed - now);
    nextAllowedEpochSeconds_[host] = std::max(now, nextAllowed) + delay;
  }
  if (waitSeconds > 0) {
    std::this_thread::sleep_for(std::chrono::duration<double>(waitSeconds));
  }
}

void Crawler::enqueueIfNew(const std::string& url) {
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return;
  std::lock_guard<std::mutex> lock(frontierMutex_);
  if (seen_.count(url)) return;
  if (frontier_.size() >= options_.maxFrontierSize) return;
  seen_.insert(url);
  frontier_.push_back(url);
  frontierCv_.notify_one();
}

CrawlStats Crawler::crawl(const std::vector<std::string>& seedUrls,
                           const std::string& outputDir) {
#ifdef ENGINE_HAVE_CURL
  static std::once_flag curlInitFlag;
  std::call_once(curlInitFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
#endif

  fs::create_directories(outputDir);
  fs::create_directories(fs::path(outputDir) / "pages");

  // --- restore checkpoint from a previous (possibly truncated) run ---
  {
    std::ifstream seenIn(fs::path(outputDir) / "seen.txt");
    std::string line;
    while (std::getline(seenIn, line)) {
      if (!line.empty()) seen_.insert(line);
    }
  }
  {
    std::ifstream frontierIn(fs::path(outputDir) / "frontier.txt");
    std::string line;
    while (std::getline(frontierIn, line)) {
      if (!line.empty()) frontier_.push_back(line);
    }
  }
  for (const auto& seed : seedUrls) enqueueIfNew(seed);

  fs::path manifestPath = fs::path(outputDir) / "manifest.tsv";
  uint32_t startId = 0;
  if (fs::exists(manifestPath)) {
    std::ifstream in(manifestPath);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
      if (first) {
        first = false;
        if (!line.empty() && line[0] == '#') continue;
      }
      size_t tab = line.find('\t');
      if (tab == std::string::npos) continue;
      try {
        uint32_t id = static_cast<uint32_t>(std::stoul(line.substr(0, tab)));
        startId = std::max(startId, id + 1);
      } catch (...) {
      }
    }
  } else {
    std::ofstream initial(manifestPath);
    initial << "# search-engine crawl manifest v1\n";
  }
  nextDocId_ = startId;

  CrawlStats stats;
  std::mutex statsMutex;

  auto worker = [&]() {
    for (;;) {
      std::string url;
      {
        std::unique_lock<std::mutex> lock(frontierMutex_);
        for (;;) {
          if (fetchedCount_.load() >= options_.maxPages) {
            draining_ = true;
            frontierCv_.notify_all();
            return;
          }
          if (!frontier_.empty()) {
            url = frontier_.front();
            frontier_.pop_front();
            ++activeWorkers_;
            break;
          }
          if (activeWorkers_ == 0) {
            draining_ = true;
            frontierCv_.notify_all();
            return;
          }
          frontierCv_.wait_for(lock, std::chrono::milliseconds(50));
        }
      }

      std::string host = hostOf(url);
      bool allowed = true;
      double robotsDelay = 0.0;
      if (!host.empty() && options_.obeyRobots) {
        RobotsRules& robots = robotsForHost(url);
        allowed = robots.isAllowed(urlPath(url));
        robotsDelay = robots.crawlDelaySeconds();
      }
      if (!host.empty() && allowed) {
        double delay = std::max(options_.politenessDelaySeconds, robotsDelay);
        politeWait(host, delay);
      }

      if (!allowed || host.empty()) {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.pagesSkippedRobots++;
      } else {
        std::string body;
        long status = 0;
        if (fetch(url, body, status) && status >= 200 && status < 300) {
          ParsedHtml parsed = parseHtml(body, url);
          uint32_t id = nextDocId_.fetch_add(1);
          std::string relPath = "pages/" + zeroPadded(id) + ".html";
          {
            std::ofstream out(fs::path(outputDir) / relPath, std::ios::binary);
            out << body;
          }
          std::vector<std::string> outlinkUrls;
          for (const auto& link : parsed.links) {
            if (outlinkUrls.size() >= options_.maxOutlinksPerPage) break;
            if (link.url.rfind("http://", 0) != 0 &&
                link.url.rfind("https://", 0) != 0) {
              continue;
            }
            outlinkUrls.push_back(link.url);
            enqueueIfNew(link.url);
          }
          {
            std::lock_guard<std::mutex> lock(manifestMutex_);
            std::ofstream manOut(manifestPath, std::ios::app);
            manOut << id << '\t' << relPath << '\t' << url << '\t'
                   << sanitizeForTsv(parsed.title) << '\t';
            for (size_t i = 0; i < outlinkUrls.size(); ++i) {
              if (i) manOut << ',';
              manOut << outlinkUrls[i];
            }
            manOut << '\n';
          }
          fetchedCount_++;
          std::lock_guard<std::mutex> lock(statsMutex);
          stats.pagesFetched++;
        } else {
          std::lock_guard<std::mutex> lock(statsMutex);
          stats.pagesFailed++;
        }
      }

      {
        std::lock_guard<std::mutex> lock(frontierMutex_);
        --activeWorkers_;
      }
      frontierCv_.notify_all();
    }
  };

  std::vector<std::thread> workers;
  size_t numThreads = std::max<size_t>(1, options_.numThreads);
  for (size_t i = 0; i < numThreads; ++i) workers.emplace_back(worker);
  for (auto& t : workers) t.join();

  // --- checkpoint remaining state so a future run can resume ---
  {
    std::ofstream seenOut(fs::path(outputDir) / "seen.txt");
    for (const auto& u : seen_) seenOut << u << '\n';
  }
  {
    std::ofstream frontierOut(fs::path(outputDir) / "frontier.txt");
    for (const auto& u : frontier_) frontierOut << u << '\n';
  }

  return stats;
}

}  // namespace engine
