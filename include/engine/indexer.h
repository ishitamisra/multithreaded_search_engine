#pragma once
// Level 1.2 ("traverse a directory on disk, indexing the content in a
// hash table in memory") and Level 2.6 ("parallelize the tasks of
// crawling, parsing and index building"): walks a directory of documents
// (crawled pages or a local text/HTML corpus), tokenizes each file in
// parallel across a thread pool, and merges the per-thread hash tables
// into one final in-memory index that IndexWriter can flush to disk.

#include <string>
#include <vector>

#include "engine/common.h"
#include "engine/index_writer.h"

namespace engine {

struct IndexerOptions {
  size_t numThreads = 4;
  bool removeStopWords = false;
  bool applyStemming = false;
  // Extensions treated as HTML (title/link extraction); anything else is
  // indexed as plain text.
  std::vector<std::string> htmlExtensions = {".html", ".htm"};
};

class Indexer {
 public:
  explicit Indexer(IndexerOptions options = {});

  // Recursively walks `directory`, indexing every regular file found.
  // Populates `postings` and `documents` (both output parameters so
  // build_index_main can pass the result straight to IndexWriter).
  void build(const std::string& directory, InMemoryIndex& postings,
             std::vector<DocumentMeta>& documents);

 private:
  IndexerOptions options_;
};

}  // namespace engine
