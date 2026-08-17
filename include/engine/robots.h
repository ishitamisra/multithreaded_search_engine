#pragma once
// Minimal robots.txt parser: enough of the de-facto standard to be a
// polite crawler (Level 2.4) -- User-agent groups, Allow/Disallow paths
// (longest match wins, Allow breaks ties), and Crawl-delay.

#include <string>
#include <vector>

namespace engine {

class RobotsRules {
 public:
  // Parses robots.txt `content`, keeping only rules from the group that
  // matches `userAgent` (falling back to the "*" group if there is no
  // exact match, per the spec).
  static RobotsRules parse(const std::string& content,
                            const std::string& userAgent = "*");

  bool isAllowed(const std::string& path) const;
  double crawlDelaySeconds() const { return crawlDelaySeconds_; }

 private:
  struct Rule {
    std::string prefix;
    bool allow;
  };
  std::vector<Rule> rules_;
  double crawlDelaySeconds_ = 0.0;
};

}  // namespace engine
