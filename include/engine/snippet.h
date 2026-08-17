#pragma once
// Level 7.3: build a short, readable excerpt for a search hit by finding
// the token window with the highest density of matched query terms
// (a simple, effective heuristic used by most real snippet generators).

#include <string>
#include <vector>

#include "engine/tokenizer.h"

namespace engine {

class SnippetGenerator {
 public:
  explicit SnippetGenerator(TokenizerOptions tokenizerOptions = {});

  // `bodyText` is the plain-text content to excerpt from (already
  // HTML-stripped, if applicable -- see ParsedHtml::text). `queryTerms`
  // should already be normalized (QueryParser::ParsedQuery::terms).
  // `windowTokens` bounds how many tokens the excerpt spans.
  std::string generate(const std::string& bodyText,
                        const std::vector<std::string>& queryTerms,
                        size_t windowTokens = 25) const;

  // Wraps case-insensitive occurrences of `terms` in `<mark>...</mark>`
  // and HTML-escapes everything else, for rendering in the HTTP UI.
  static std::string highlightHtml(const std::string& snippet,
                                    const std::vector<std::string>& terms);

 private:
  Tokenizer tokenizer_;
};

}  // namespace engine
