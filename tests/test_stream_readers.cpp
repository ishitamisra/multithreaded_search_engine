#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>

#include "engine/index_reader.h"
#include "engine/index_writer.h"
#include "engine/stream_reader.h"
#include "test_framework.h"

using namespace engine;
namespace fs = std::filesystem;

namespace {
std::set<DocId> collect(std::unique_ptr<StreamReader> stream) {
  std::set<DocId> result;
  for (; stream->valid(); stream->next()) result.insert(stream->docId());
  return result;
}
}  // namespace

TEST_MAIN_BEGIN()
  // A tiny 4-document corpus, built directly (not via Indexer) so the
  // postings and positions are exactly what this test expects:
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
  add("the", 0, {0});
  add("the", 1, {0});
  add("quick", 0, {1});
  add("quick", 1, {1});
  add("brown", 0, {2});
  add("brown", 2, {0});
  add("fox", 0, {3});
  add("fox", 1, {2});
  add("fox", 2, {1});
  add("jumps", 1, {3});
  add("jumps", 2, {2});
  add("over", 2, {3});
  add("lazy", 3, {0});
  add("dog", 3, {1});
  add("sleeps", 3, {2});

  std::vector<DocumentMeta> docs(4);
  std::vector<uint32_t> lengths = {4, 4, 4, 3};
  for (DocId i = 0; i < 4; ++i) {
    docs[i].id = i;
    docs[i].title = "Doc " + std::to_string(i);
    docs[i].url = "doc://" + std::to_string(i);
    docs[i].length = lengths[i];
  }

  fs::path dir = fs::temp_directory_path() / "sereng_test_stream_readers";
  fs::create_directories(dir);
  std::string prefix = (dir / "idx").string();
  IndexWriter::write(prefix, idx, docs);
  IndexReader reader(prefix);

  auto termStream = [&](const std::string& w) {
    return std::unique_ptr<StreamReader>(new TermStream(reader.openPostings(w)));
  };
  auto phraseTerm = [&](const std::string& w) {
    return std::unique_ptr<TermStream>(new TermStream(reader.openPostings(w)));
  };

  // AND(quick, fox) => {0, 1}
  {
    std::vector<std::unique_ptr<StreamReader>> children;
    children.push_back(termStream("quick"));
    children.push_back(termStream("fox"));
    auto result = collect(std::make_unique<AndStream>(std::move(children)));
    CHECK_EQ(result, (std::set<DocId>{0, 1}));
  }

  // OR(brown, lazy) => {0, 2, 3}
  {
    std::vector<std::unique_ptr<StreamReader>> children;
    children.push_back(termStream("brown"));
    children.push_back(termStream("lazy"));
    auto result = collect(std::make_unique<OrStream>(std::move(children)));
    CHECK_EQ(result, (std::set<DocId>{0, 2, 3}));
  }

  // NOT(fox, brown): fox is in {0,1,2}, brown is in {0,2} => {1}
  {
    auto notStream = std::make_unique<NotStream>(termStream("fox"), termStream("brown"));
    auto result = collect(std::move(notStream));
    CHECK_EQ(result, (std::set<DocId>{1}));
  }

  // PHRASE("quick","fox"): adjacent only in doc1 (quick@1, fox@2)
  {
    std::vector<std::unique_ptr<TermStream>> children;
    children.push_back(phraseTerm("quick"));
    children.push_back(phraseTerm("fox"));
    auto result = collect(std::make_unique<PhraseStream>(std::move(children)));
    CHECK_EQ(result, (std::set<DocId>{1}));
  }

  // PHRASE("brown","fox"): adjacent in doc0 (2,3) and doc2 (0,1)
  {
    std::vector<std::unique_ptr<TermStream>> children;
    children.push_back(phraseTerm("brown"));
    children.push_back(phraseTerm("fox"));
    auto result = collect(std::make_unique<PhraseStream>(std::move(children)));
    CHECK_EQ(result, (std::set<DocId>{0, 2}));
  }

  // A word absent from the index yields an empty stream, not a crash.
  {
    auto result = collect(termStream("nonexistent"));
    CHECK(result.empty());
  }

  fs::remove_all(dir);
TEST_MAIN_END()
