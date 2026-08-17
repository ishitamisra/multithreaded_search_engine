#include "engine/query_parser.h"

#include <cctype>

namespace engine {

namespace {

// ---- Lexer ----

enum class TokType { Word, Phrase, LParen, RParen, And, Or, Not, End };

struct QueryToken {
  TokType type;
  std::string text;                  // Word
  std::vector<std::string> phrase;   // Phrase (already split into raw words)
};

std::vector<QueryToken> lex(const std::string& query) {
  std::vector<QueryToken> tokens;
  size_t i = 0;
  const size_t n = query.size();
  while (i < n) {
    unsigned char c = query[i];
    if (std::isspace(c)) {
      ++i;
      continue;
    }
    if (c == '(') {
      tokens.push_back({TokType::LParen, "", {}});
      ++i;
      continue;
    }
    if (c == ')') {
      tokens.push_back({TokType::RParen, "", {}});
      ++i;
      continue;
    }
    if (c == '"') {
      size_t end = query.find('"', i + 1);
      std::string inner =
          query.substr(i + 1, (end == std::string::npos ? n : end) - i - 1);
      QueryToken tok{TokType::Phrase, "", {}};
      std::string word;
      for (char pc : inner) {
        if (std::isalnum(static_cast<unsigned char>(pc))) {
          word.push_back(pc);
        } else if (!word.empty()) {
          tok.phrase.push_back(word);
          word.clear();
        }
      }
      if (!word.empty()) tok.phrase.push_back(word);
      tokens.push_back(std::move(tok));
      i = (end == std::string::npos) ? n : end + 1;
      continue;
    }
    if (c == '-') {
      tokens.push_back({TokType::Not, "-", {}});
      ++i;
      continue;
    }
    if (std::isalnum(c)) {
      size_t start = i;
      while (i < n && std::isalnum(static_cast<unsigned char>(query[i]))) ++i;
      std::string word = query.substr(start, i - start);
      std::string upper = word;
      for (char& ch : upper) ch = static_cast<char>(std::toupper((unsigned char)ch));
      if (upper == "AND") {
        tokens.push_back({TokType::And, word, {}});
      } else if (upper == "OR") {
        tokens.push_back({TokType::Or, word, {}});
      } else if (upper == "NOT") {
        tokens.push_back({TokType::Not, word, {}});
      } else {
        tokens.push_back({TokType::Word, word, {}});
      }
      continue;
    }
    // unrecognized character: skip it
    ++i;
  }
  tokens.push_back({TokType::End, "", {}});
  return tokens;
}

// ---- AST ----

enum class NodeType { Word, Phrase, And, Or };

struct Node {
  NodeType type;
  std::string word;
  std::vector<std::string> phraseWords;
  std::vector<std::unique_ptr<Node>> children;  // And: positives, Or: branches
  std::vector<std::unique_ptr<Node>> excluded;   // And only: negated terms
};
using NodePtr = std::unique_ptr<Node>;

// ---- Recursive-descent parser over the token stream ----

class Parser {
 public:
  explicit Parser(std::vector<QueryToken> tokens) : tokens_(std::move(tokens)) {}

  NodePtr parseQuery() {
    NodePtr node = parseOr();
    if (peek().type != TokType::End) {
      throw QueryParseError("unexpected trailing input near '" +
                             peek().text + "'");
    }
    return node;
  }

 private:
  const QueryToken& peek() const { return tokens_[pos_]; }
  const QueryToken& advance() { return tokens_[pos_++]; }

  NodePtr parseOr() {
    NodePtr left = parseAnd();
    if (peek().type != TokType::Or) return left;
    auto orNode = std::make_unique<Node>();
    orNode->type = NodeType::Or;
    orNode->children.push_back(std::move(left));
    while (peek().type == TokType::Or) {
      advance();
      orNode->children.push_back(parseAnd());
    }
    return orNode;
  }

  NodePtr parseAnd() {
    auto andNode = std::make_unique<Node>();
    andNode->type = NodeType::And;
    parseAndTerm(*andNode);
    for (;;) {
      TokType t = peek().type;
      if (t == TokType::And) {
        advance();
        parseAndTerm(*andNode);
      } else if (t == TokType::Word || t == TokType::Phrase ||
                 t == TokType::LParen || t == TokType::Not) {
        parseAndTerm(*andNode);  // implicit AND via juxtaposition
      } else {
        break;
      }
    }
    if (andNode->children.empty()) {
      throw QueryParseError(
          "a query clause must contain at least one non-negated term");
    }
    if (andNode->children.size() == 1 && andNode->excluded.empty()) {
      return std::move(andNode->children.front());
    }
    return andNode;
  }

  void parseAndTerm(Node& andNode) {
    bool negated = false;
    if (peek().type == TokType::Not) {
      negated = true;
      advance();
    }
    NodePtr primary = parsePrimary();
    if (negated) {
      andNode.excluded.push_back(std::move(primary));
    } else {
      andNode.children.push_back(std::move(primary));
    }
  }

  NodePtr parsePrimary() {
    const QueryToken& tok = peek();
    if (tok.type == TokType::Word) {
      advance();
      auto node = std::make_unique<Node>();
      node->type = NodeType::Word;
      node->word = tok.text;
      return node;
    }
    if (tok.type == TokType::Phrase) {
      advance();
      if (tok.phrase.empty()) {
        throw QueryParseError("empty phrase in query");
      }
      auto node = std::make_unique<Node>();
      node->type = NodeType::Phrase;
      node->phraseWords = tok.phrase;
      return node;
    }
    if (tok.type == TokType::LParen) {
      advance();
      NodePtr inner = parseOr();
      if (peek().type != TokType::RParen) {
        throw QueryParseError("missing closing ')'");
      }
      advance();
      return inner;
    }
    throw QueryParseError("expected a term, phrase, or '(' near '" +
                           tok.text + "'");
  }

  std::vector<QueryToken> tokens_;
  size_t pos_ = 0;
};

// An always-empty stream, used when a query word never appears in the
// index -- keeps AND/OR/NOT/PHRASE compilation total (no special-casing
// "word not found" at every call site).
class EmptyStream : public StreamReader {
 public:
  bool valid() const override { return false; }
  DocId docId() const override { return kInvalidDocId; }
  void next() override {}
  void seek(DocId) override {}
};

}  // namespace

QueryParser::QueryParser(const IndexReader& reader,
                          TokenizerOptions tokenizerOptions)
    : reader_(reader), tokenizer_(tokenizerOptions) {}

namespace {

std::string normalizeWord(const Tokenizer& tokenizer, const std::string& raw) {
  auto tokens = tokenizer.tokenize(raw);
  return tokens.empty() ? std::string() : tokens.front();
}

std::unique_ptr<StreamReader> openTerm(const IndexReader& reader,
                                        const std::string& normalized) {
  auto postings = reader.openPostings(normalized);
  if (!postings) return std::make_unique<EmptyStream>();
  return std::make_unique<TermStream>(std::move(postings));
}

std::unique_ptr<TermStream> openTermConcrete(const IndexReader& reader,
                                              const std::string& normalized) {
  auto postings = reader.openPostings(normalized);
  if (!postings) {
    // A phrase containing a word absent from the index can never match;
    // give it a TermStream over an already-exhausted reader by opening a
    // real (empty-result) postings run: reuse openPostings with a
    // sentinel word that is guaranteed absent, if not present already.
    return nullptr;
  }
  return std::make_unique<TermStream>(std::move(postings));
}

std::unique_ptr<StreamReader> compile(const Node& node, const IndexReader& reader,
                                       const Tokenizer& tokenizer) {
  switch (node.type) {
    case NodeType::Word: {
      std::string normalized = normalizeWord(tokenizer, node.word);
      return openTerm(reader, normalized);
    }
    case NodeType::Phrase: {
      std::vector<std::unique_ptr<TermStream>> children;
      for (const auto& raw : node.phraseWords) {
        std::string normalized = normalizeWord(tokenizer, raw);
        auto term = openTermConcrete(reader, normalized);
        if (!term) return std::make_unique<EmptyStream>();
        children.push_back(std::move(term));
      }
      return std::make_unique<PhraseStream>(std::move(children));
    }
    case NodeType::Or: {
      std::vector<std::unique_ptr<StreamReader>> branches;
      for (const auto& child : node.children) {
        branches.push_back(compile(*child, reader, tokenizer));
      }
      return std::make_unique<OrStream>(std::move(branches));
    }
    case NodeType::And: {
      std::unique_ptr<StreamReader> combined;
      if (node.children.size() == 1) {
        combined = compile(*node.children.front(), reader, tokenizer);
      } else {
        std::vector<std::unique_ptr<StreamReader>> positives;
        for (const auto& child : node.children) {
          positives.push_back(compile(*child, reader, tokenizer));
        }
        combined = std::make_unique<AndStream>(std::move(positives));
      }
      for (const auto& excl : node.excluded) {
        combined = std::make_unique<NotStream>(std::move(combined),
                                                 compile(*excl, reader, tokenizer));
      }
      return combined;
    }
  }
  throw QueryParseError("internal error: unhandled node type");
}

void collectPositiveTerms(const Node& node, const Tokenizer& tokenizer,
                           std::vector<std::string>& out) {
  switch (node.type) {
    case NodeType::Word:
      out.push_back(normalizeWord(tokenizer, node.word));
      return;
    case NodeType::Phrase:
      for (const auto& w : node.phraseWords)
        out.push_back(normalizeWord(tokenizer, w));
      return;
    case NodeType::Or:
    case NodeType::And:
      for (const auto& c : node.children) collectPositiveTerms(*c, tokenizer, out);
      return;  // note: node.excluded deliberately skipped
  }
}

}  // namespace

QueryParser::ParsedQuery QueryParser::parse(const std::string& queryText) const {
  if (queryText.find_first_not_of(" \t\r\n") == std::string::npos) {
    throw QueryParseError("empty query");
  }
  Parser parser(lex(queryText));
  NodePtr ast = parser.parseQuery();

  ParsedQuery result;
  result.stream = compile(*ast, reader_, tokenizer_);
  collectPositiveTerms(*ast, tokenizer_, result.terms);
  return result;
}

}  // namespace engine
