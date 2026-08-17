#pragma once
// Turns raw text into a normalized token stream: lowercased, alphanumeric
// "words", with optional stopword filtering and a light suffix-stripping
// stemmer. This is Level 1 ("parse text files into tokens") plus the
// optional Level 5 stemming/stopword extension.

#include <string>
#include <unordered_set>
#include <vector>

namespace engine {

struct TokenizerOptions {
  bool removeStopWords = false;
  bool applyStemming = false;
  size_t minTokenLength = 1;
};

class Tokenizer {
 public:
  explicit Tokenizer(TokenizerOptions options = {});

  // Splits `text` into normalized tokens, in order. Positions in the
  // returned vector correspond 1:1 with word-position indices used
  // elsewhere (phrase queries, snippets).
  std::vector<std::string> tokenize(const std::string& text) const;

  struct TokenSpan {
    std::string word;  // normalized, same as tokenize() would produce
    size_t start;       // byte offset of the raw token in `text`
    size_t end;          // one past the last byte of the raw token
  };
  // Like tokenize(), but also reports the original byte range of each
  // token so callers (snippet generation) can slice readable substrings
  // out of the untouched source text.
  std::vector<TokenSpan> tokenizeWithOffsets(const std::string& text) const;

  static bool isStopWord(const std::string& word);

  // Very small suffix-stripping stemmer (not a full Porter stemmer):
  // strips a handful of common English suffixes. Documented as a
  // best-effort normalization step, not linguistically exhaustive.
  static std::string stem(const std::string& word);

  static std::string toLower(const std::string& word);

 private:
  TokenizerOptions options_;
};

}  // namespace engine
