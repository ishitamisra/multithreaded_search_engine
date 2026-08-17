#include "engine/tokenizer.h"

#include <algorithm>
#include <cctype>

namespace engine {

namespace {
const std::unordered_set<std::string>& stopWordSet() {
  static const std::unordered_set<std::string> kStopWords = {
      "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
      "has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
      "to", "was", "were", "will", "with", "this", "but", "not", "or",
      "you", "your", "i", "we", "they", "his", "her", "their", "have",
      "had", "if", "so", "do", "does", "did", "can", "could", "would",
      "should", "about", "into", "than", "then", "there", "these",
      "those", "which", "who", "whom", "what", "when", "where", "why",
      "how"};
  return kStopWords;
}
}  // namespace

Tokenizer::Tokenizer(TokenizerOptions options) : options_(options) {}

std::string Tokenizer::toLower(const std::string& word) {
  std::string out;
  out.reserve(word.size());
  for (char c : word) {
    out.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool Tokenizer::isStopWord(const std::string& word) {
  return stopWordSet().count(word) != 0;
}

std::string Tokenizer::stem(const std::string& word) {
  static const std::vector<std::string> kSuffixes = {
      "ational", "tional", "izer", "ation", "ing", "edly", "ies", "ed",
      "es", "ly", "s"};
  for (const auto& suffix : kSuffixes) {
    if (word.size() > suffix.size() + 2 &&
        word.compare(word.size() - suffix.size(), suffix.size(), suffix) ==
            0) {
      return word.substr(0, word.size() - suffix.size());
    }
  }
  return word;
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
  std::vector<std::string> tokens;
  std::string current;
  current.reserve(32);

  auto flush = [&]() {
    if (current.empty()) return;
    std::string word = toLower(current);
    current.clear();
    if (word.size() < options_.minTokenLength) return;
    if (options_.removeStopWords && isStopWord(word)) return;
    if (options_.applyStemming) word = stem(word);
    if (word.empty()) return;
    tokens.push_back(std::move(word));
  };

  for (unsigned char c : text) {
    if (std::isalnum(c)) {
      current.push_back(static_cast<char>(c));
    } else if (c == '\'' && !current.empty()) {
      // allow apostrophes inside words (don't -> dont after stripping
      // trailing 's below), but don't let a leading/trailing quote in.
      continue;
    } else {
      flush();
    }
  }
  flush();
  return tokens;
}

std::vector<Tokenizer::TokenSpan> Tokenizer::tokenizeWithOffsets(
    const std::string& text) const {
  std::vector<TokenSpan> spans;
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    if (!std::isalnum(static_cast<unsigned char>(text[i]))) {
      ++i;
      continue;
    }
    size_t start = i;
    while (i < n && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
    std::string raw = text.substr(start, i - start);
    std::string word = toLower(raw);
    if (word.size() < options_.minTokenLength) continue;
    if (options_.removeStopWords && isStopWord(word)) continue;
    if (options_.applyStemming) word = stem(word);
    if (word.empty()) continue;
    spans.push_back(TokenSpan{std::move(word), start, i});
  }
  return spans;
}

}  // namespace engine
