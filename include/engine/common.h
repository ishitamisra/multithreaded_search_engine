#pragma once
// Shared value types used across the engine: document identifiers, posting
// entries (word -> document occurrence) and per-document metadata. Kept
// dependency-free so every other header can include it cheaply.

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

using DocId = uint32_t;
constexpr DocId kInvalidDocId = static_cast<DocId>(-1);

// One occurrence record for a word in a single document: which document,
// how many times the word appears, and the token-index positions it
// appears at (positions drive phrase queries and snippet generation).
struct Posting {
  DocId docId = kInvalidDocId;
  uint32_t termFreq = 0;
  std::vector<uint32_t> positions;
};

// Metadata about one crawled/indexed document, kept small enough to load
// entirely into memory even for large corpora (the raw text lives on disk).
struct DocumentMeta {
  DocId id = kInvalidDocId;
  std::string url;    // original URL, or "file://" + path for local corpora
  std::string path;    // path to the stored raw text on disk
  std::string title;
  uint32_t length = 0;  // number of tokens
  double staticScore = 0.0;  // e.g. PageRank score, filled in later
  std::vector<DocId> outlinks;  // resolved link targets discovered at crawl time
};

}  // namespace engine
