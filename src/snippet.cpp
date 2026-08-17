#include "engine/snippet.h"

#include <algorithm>
#include <unordered_set>

namespace engine {

namespace {
std::string collapseWhitespace(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool lastWasSpace = false;
  for (char c : s) {
    bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    if (isSpace) {
      if (!lastWasSpace && !out.empty()) out.push_back(' ');
      lastWasSpace = true;
    } else {
      out.push_back(c);
      lastWasSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::string htmlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}
}  // namespace

SnippetGenerator::SnippetGenerator(TokenizerOptions tokenizerOptions)
    : tokenizer_(tokenizerOptions) {}

std::string SnippetGenerator::generate(const std::string& bodyText,
                                        const std::vector<std::string>& queryTerms,
                                        size_t windowTokens) const {
  auto spans = tokenizer_.tokenizeWithOffsets(bodyText);
  if (spans.empty()) {
    std::string fallback = collapseWhitespace(bodyText.substr(0, 240));
    return fallback;
  }
  std::unordered_set<std::string> terms(queryTerms.begin(), queryTerms.end());
  windowTokens = std::min(windowTokens, spans.size());
  if (windowTokens == 0) windowTokens = spans.size();

  size_t bestStart = 0;
  size_t bestCount = 0;
  size_t currentCount = 0;
  for (size_t i = 0; i < spans.size(); ++i) {
    if (terms.count(spans[i].word)) ++currentCount;
    if (i >= windowTokens) {
      if (terms.count(spans[i - windowTokens].word)) --currentCount;
    }
    if (i + 1 >= windowTokens) {
      size_t windowStart = i + 1 - windowTokens;
      if (currentCount > bestCount) {
        bestCount = currentCount;
        bestStart = windowStart;
      }
    }
  }
  size_t windowEnd = std::min(spans.size(), bestStart + windowTokens);

  size_t charStart = spans[bestStart].start;
  size_t charEnd = spans[windowEnd - 1].end;
  std::string excerpt = collapseWhitespace(bodyText.substr(charStart, charEnd - charStart));

  std::string prefix = (bestStart > 0) ? "... " : "";
  std::string suffix = (windowEnd < spans.size()) ? " ..." : "";
  return prefix + excerpt + suffix;
}

std::string SnippetGenerator::highlightHtml(const std::string& snippet,
                                             const std::vector<std::string>& terms) {
  Tokenizer tokenizer;
  auto spans = tokenizer.tokenizeWithOffsets(snippet);
  std::string out;
  out.reserve(snippet.size() + 32);
  size_t cursor = 0;
  for (const auto& span : spans) {
    bool matches = false;
    for (const auto& term : terms) {
      if (term.empty()) continue;
      if (span.word == term || span.word.rfind(term, 0) == 0 ||
          term.rfind(span.word, 0) == 0) {
        matches = true;
        break;
      }
    }
    out += htmlEscape(snippet.substr(cursor, span.start - cursor));
    std::string raw = snippet.substr(span.start, span.end - span.start);
    if (matches) {
      out += "<mark>" + htmlEscape(raw) + "</mark>";
    } else {
      out += htmlEscape(raw);
    }
    cursor = span.end;
  }
  out += htmlEscape(snippet.substr(cursor));
  return out;
}

}  // namespace engine
