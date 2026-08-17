#pragma once
// Level 5.4: a top-down recursive-descent parser that compiles a query
// string into a StreamReader tree (the "index stream readers" from
// stream_reader.h). Grammar (lowest to highest precedence):
//
//   Query    := OrExpr
//   OrExpr   := AndExpr ( "OR" AndExpr )*
//   AndExpr  := AndTerm ( ["AND"] AndTerm )*        -- juxtaposition == AND
//   AndTerm  := ["NOT" | "-"] Primary
//   Primary  := WORD | "\"" WORD+ "\"" | "(" OrExpr ")"
//
// Negation (NOT / leading "-") is resolved within its enclosing AndExpr:
// "cats -dogs" compiles to AND(cats) with dogs subtracted via NotStream,
// which is why a clause consisting only of negated terms is a parse
// error (there is nothing to subtract from).

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/index_reader.h"
#include "engine/stream_reader.h"
#include "engine/tokenizer.h"

namespace engine {

class QueryParseError : public std::runtime_error {
 public:
  explicit QueryParseError(const std::string& msg) : std::runtime_error(msg) {}
};

class QueryParser {
 public:
  struct ParsedQuery {
    std::unique_ptr<StreamReader> stream;
    // Normalized positive words (single words and phrase words, but not
    // anything only appearing under a NOT) -- used by Ranker for scoring
    // and by the snippet generator to find something to highlight.
    std::vector<std::string> terms;
  };

  explicit QueryParser(const IndexReader& reader,
                        TokenizerOptions tokenizerOptions = {});

  // Throws QueryParseError on malformed input (unbalanced parens, an
  // empty query, a clause that is entirely negated terms, etc).
  ParsedQuery parse(const std::string& queryText) const;

 private:
  const IndexReader& reader_;
  Tokenizer tokenizer_;
};

}  // namespace engine
