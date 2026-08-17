#include <filesystem>
#include <set>
#include <vector>

#include "engine/index_reader.h"
#include "engine/index_writer.h"
#include "engine/query_parser.h"
#include "engine/ranker.h"
#include "test_framework.h"

using namespace engine;
namespace fs = std::filesystem;

namespace {
std::set<DocId> run(const IndexReader& reader, const std::string& query,
                     std::vector<std::string>* termsOut = nullptr) {
  QueryParser parser(reader);
  auto parsed = parser.parse(query);
  std::set<DocId> result;
  for (auto& s = parsed.stream; s->valid(); s->next()) result.insert(s->docId());
  if (termsOut) *termsOut = parsed.terms;
  return result;
}
}  // namespace

TEST_MAIN_BEGIN()
  // Same 4-document corpus as test_stream_readers.cpp:
  //   doc0: "the quick brown fox"
  //   doc1: "the quick fox jumps"
  //   doc2: "brown fox jumps over"
  //   doc3: "lazy dog sleeps"
  InMemoryIndex idx;
  auto add = [&](const std::string& word, DocId doc, std::vector<uint32_t> positions) {
    Posting p;
    p.docId = doc;
    p.termFreq = static_cast<uint32_t>(positions.size());
    p.positions = std::move(positions);
    idx.getOrInsert(word).push_back(p);
  };
  add("the", 0, {0}); add("the", 1, {0});
  add("quick", 0, {1}); add("quick", 1, {1});
  add("brown", 0, {2}); add("brown", 2, {0});
  add("fox", 0, {3}); add("fox", 1, {2}); add("fox", 2, {1});
  add("jumps", 1, {3}); add("jumps", 2, {2});
  add("over", 2, {3});
  add("lazy", 3, {0}); add("dog", 3, {1}); add("sleeps", 3, {2});

  std::vector<DocumentMeta> docs(4);
  std::vector<uint32_t> lengths = {4, 4, 4, 3};
  for (DocId i = 0; i < 4; ++i) {
    docs[i].id = i;
    docs[i].title = "Doc " + std::to_string(i);
    docs[i].url = "doc://" + std::to_string(i);
    docs[i].length = lengths[i];
  }

  fs::path dir = fs::temp_directory_path() / "sereng_test_query_parser";
  fs::create_directories(dir);
  std::string prefix = (dir / "idx").string();
  IndexWriter::write(prefix, idx, docs);
  IndexReader reader(prefix);

  CHECK_EQ(run(reader, "quick fox"), (std::set<DocId>{0, 1}));       // implicit AND
  CHECK_EQ(run(reader, "quick AND fox"), (std::set<DocId>{0, 1}));
  CHECK_EQ(run(reader, "brown OR lazy"), (std::set<DocId>{0, 2, 3}));
  CHECK_EQ(run(reader, "fox -brown"), (std::set<DocId>{1}));
  CHECK_EQ(run(reader, "fox NOT brown"), (std::set<DocId>{1}));
  CHECK_EQ(run(reader, "\"quick fox\""), (std::set<DocId>{1}));
  CHECK_EQ(run(reader, "\"brown fox\""), (std::set<DocId>{0, 2}));
  CHECK_EQ(run(reader, "(quick OR lazy) AND NOT dog"), (std::set<DocId>{0, 1}));
  CHECK_EQ(run(reader, "nosuchword"), (std::set<DocId>{}));

  std::vector<std::string> terms;
  run(reader, "quick -brown", &terms);
  CHECK_EQ(terms.size(), (size_t)1);
  CHECK_EQ(terms[0], std::string("quick"));  // negated terms excluded from ranking terms

  bool threw = false;
  try {
    QueryParser parser(reader);
    parser.parse("-brown");  // an all-negated clause is a parse error
  } catch (const QueryParseError&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    QueryParser parser(reader);
    parser.parse("(quick OR fox");  // unbalanced parens
  } catch (const QueryParseError&) {
    threw = true;
  }
  CHECK(threw);

  // Ranker sanity check: doc0 matches both "quick" and "fox", doc2 only
  // matches "fox", so doc0 should outrank doc2 for the query "quick fox"
  // even though both satisfy an OR of the two terms.
  {
    auto matched = run(reader, "quick OR fox");
    std::vector<DocId> matchedVec(matched.begin(), matched.end());
    Ranker ranker(reader);
    auto results = ranker.rank({"quick", "fox"}, matchedVec, 10);
    CHECK(!results.empty());
    auto rankOf = [&](DocId id) -> int {
      for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].docId == id) return static_cast<int>(i);
      }
      return -1;
    };
    int rank0 = rankOf(0);
    int rank2 = rankOf(2);
    CHECK(rank0 >= 0 && rank2 >= 0);
    CHECK(rank0 < rank2);
  }

  fs::remove_all(dir);
TEST_MAIN_END()
