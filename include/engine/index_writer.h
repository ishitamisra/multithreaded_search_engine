#pragma once
// Serializes an in-memory index (built by Indexer) to the three-file
// on-disk format described in index_format.h.

#include <string>
#include <vector>

#include "engine/common.h"
#include "engine/hashtable.h"

namespace engine {

using PostingsList = std::vector<Posting>;
using InMemoryIndex = HashTable<std::string, PostingsList>;

class IndexWriter {
 public:
  // `pathPrefix` becomes pathPrefix + ".docs" / ".lex" / ".post".
  // `postings` for each word must already be sorted by ascending DocId
  // (Indexer::build() guarantees this).
  static void write(const std::string& pathPrefix,
                     const InMemoryIndex& postings,
                     const std::vector<DocumentMeta>& documents);
};

}  // namespace engine
