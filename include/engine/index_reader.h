#pragma once
// Loads an index written by IndexWriter: the document table and lexicon
// are read fully into memory (Level 3.5, "demonstrate you can read the
// index"); postings are streamed lazily from foo.post on demand so query
// time and memory stay proportional to the terms actually queried, not
// the size of the whole corpus. This is also the "index stream reader for
// words" called for in Level 3.6.

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/common.h"

namespace engine {

// Sequential cursor over one word's postings, sorted by ascending DocId.
// StreamReader (AND/OR/NOT/PHRASE) in stream_reader.h composes these into
// query execution trees via next()/seek() merge-join, the classic
// inverted-index intersection algorithm.
class PostingsStreamReader {
 public:
  PostingsStreamReader(const std::string& postingsPath, uint64_t offset,
                        uint32_t count);
  ~PostingsStreamReader();
  PostingsStreamReader(const PostingsStreamReader&) = delete;
  PostingsStreamReader& operator=(const PostingsStreamReader&) = delete;

  bool valid() const { return remaining_ > 0 || currentLoaded_; }
  DocId docId() const { return currentDocId_; }
  uint32_t termFreq() const { return currentTermFreq_; }
  const std::vector<uint32_t>& positions() const { return currentPositions_; }

  void next();
  // Advances until docId() >= target or the stream is exhausted.
  void seek(DocId target);

 private:
  void loadCurrent();

  FILE* file_;
  uint32_t remaining_;
  bool currentLoaded_ = false;
  DocId currentDocId_ = kInvalidDocId;
  uint32_t currentTermFreq_ = 0;
  std::vector<uint32_t> currentPositions_;
};

class IndexReader {
 public:
  explicit IndexReader(const std::string& pathPrefix);

  const DocumentMeta* getDocument(DocId id) const;
  size_t numDocuments() const { return documents_.size(); }
  const std::vector<DocumentMeta>& documents() const { return documents_; }

  // Returns nullptr if the word does not appear in the index at all.
  std::unique_ptr<PostingsStreamReader> openPostings(
      const std::string& word) const;

  bool contains(const std::string& word) const {
    return lexicon_.count(word) != 0;
  }
  uint32_t documentFrequency(const std::string& word) const;

 private:
  struct LexiconEntry {
    uint64_t offset;
    uint32_t count;
  };

  std::string postingsPath_;
  std::vector<DocumentMeta> documents_;
  std::unordered_map<DocId, size_t> docIndexById_;
  std::unordered_map<std::string, LexiconEntry> lexicon_;
};

}  // namespace engine
