#include <filesystem>

#include "engine/index_reader.h"
#include "engine/index_writer.h"
#include "test_framework.h"

using namespace engine;
namespace fs = std::filesystem;

TEST_MAIN_BEGIN()
  InMemoryIndex idx;
  Posting p1;
  p1.docId = 0;
  p1.termFreq = 2;
  p1.positions = {0, 5};
  idx.getOrInsert("hello").push_back(p1);

  Posting p2;
  p2.docId = 1;
  p2.termFreq = 1;
  p2.positions = {3};
  idx.getOrInsert("hello").push_back(p2);

  Posting p3;
  p3.docId = 1;
  p3.termFreq = 1;
  p3.positions = {0};
  idx.getOrInsert("world").push_back(p3);

  std::vector<DocumentMeta> docs(2);
  docs[0].id = 0;
  docs[0].url = "http://example.com/a";
  docs[0].title = "Doc A";
  docs[0].length = 10;
  docs[0].staticScore = 0.0;
  docs[0].outlinks = {1};

  docs[1].id = 1;
  docs[1].url = "http://example.com/b";
  docs[1].title = "Doc B";
  docs[1].length = 5;
  docs[1].outlinks = {};

  fs::path dir = fs::temp_directory_path() / "sereng_test_index_roundtrip";
  fs::create_directories(dir);
  std::string prefix = (dir / "idx").string();
  IndexWriter::write(prefix, idx, docs);

  IndexReader reader(prefix);
  CHECK_EQ(reader.numDocuments(), (size_t)2);

  const DocumentMeta* docA = reader.getDocument(0);
  CHECK(docA != nullptr);
  CHECK_EQ(docA->title, std::string("Doc A"));
  CHECK_EQ(docA->url, std::string("http://example.com/a"));
  CHECK_EQ(docA->length, (uint32_t)10);
  CHECK_EQ(docA->outlinks.size(), (size_t)1);
  CHECK_EQ(docA->outlinks[0], (DocId)1);

  CHECK(reader.getDocument(999) == nullptr);

  CHECK_EQ(reader.documentFrequency("hello"), (uint32_t)2);
  CHECK_EQ(reader.documentFrequency("world"), (uint32_t)1);
  CHECK_EQ(reader.documentFrequency("missing"), (uint32_t)0);
  CHECK(!reader.contains("missing"));
  CHECK(reader.contains("hello"));

  auto stream = reader.openPostings("hello");
  CHECK(stream != nullptr);
  CHECK(stream->valid());
  CHECK_EQ(stream->docId(), (DocId)0);
  CHECK_EQ(stream->termFreq(), (uint32_t)2);
  CHECK_EQ(stream->positions().size(), (size_t)2);
  CHECK_EQ(stream->positions()[0], (uint32_t)0);
  CHECK_EQ(stream->positions()[1], (uint32_t)5);
  stream->next();
  CHECK(stream->valid());
  CHECK_EQ(stream->docId(), (DocId)1);
  stream->next();
  CHECK(!stream->valid());

  CHECK(reader.openPostings("missing") == nullptr);

  fs::remove_all(dir);
TEST_MAIN_END()
