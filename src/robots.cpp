#include "engine/robots.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace engine {

namespace {
std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}
std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return out;
}
}  // namespace

RobotsRules RobotsRules::parse(const std::string& content,
                                const std::string& userAgent) {
  RobotsRules result;
  std::string lowerAgent = toLowerCopy(userAgent);

  // Pass 1: does an exact (non-"*") group match our agent? Pass 2: collect
  // rules from that group, or from "*" if no exact group exists.
  bool haveExactGroup = false;
  {
    std::istringstream in(content);
    std::string line;
    bool inMatchingGroup = false;
    while (std::getline(in, line)) {
      size_t hash = line.find('#');
      if (hash != std::string::npos) line = line.substr(0, hash);
      line = trim(line);
      if (line.empty()) continue;
      size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string key = toLowerCopy(trim(line.substr(0, colon)));
      std::string value = trim(line.substr(colon + 1));
      if (key == "user-agent") {
        inMatchingGroup = (toLowerCopy(value) == lowerAgent);
        if (inMatchingGroup) haveExactGroup = true;
      }
    }
  }

  std::istringstream in(content);
  std::string line;
  bool inTargetGroup = false;
  std::string wantedAgent = haveExactGroup ? lowerAgent : "*";
  bool sawAnyGroupYet = false;
  while (std::getline(in, line)) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    line = trim(line);
    if (line.empty()) continue;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = toLowerCopy(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));

    if (key == "user-agent") {
      sawAnyGroupYet = true;
      inTargetGroup = (toLowerCopy(value) == wantedAgent);
      continue;
    }
    if (!sawAnyGroupYet || !inTargetGroup) continue;

    if (key == "disallow") {
      if (!value.empty()) result.rules_.push_back({value, false});
    } else if (key == "allow") {
      if (!value.empty()) result.rules_.push_back({value, true});
    } else if (key == "crawl-delay") {
      try {
        result.crawlDelaySeconds_ = std::stod(value);
      } catch (...) {
      }
    }
  }
  return result;
}

bool RobotsRules::isAllowed(const std::string& path) const {
  const Rule* best = nullptr;
  for (const auto& rule : rules_) {
    if (path.compare(0, rule.prefix.size(), rule.prefix) == 0) {
      if (!best || rule.prefix.size() > best->prefix.size() ||
          (rule.prefix.size() == best->prefix.size() && rule.allow)) {
        best = &rule;
      }
    }
  }
  return best == nullptr || best->allow;
}

}  // namespace engine
