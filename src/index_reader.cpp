#include "engine/index_reader.h"

#include <stdexcept>

#include "engine/index_format.h"
#include "engine/pagerank.h"

namespace engine {

// ---------------- PostingsStreamReader ----------------

PostingsStreamReader::PostingsStreamReader(const std::string& postingsPath,
                                            uint64_t offset, uint32_t count)
    : remaining_(count) {
  file_ = std::fopen(postingsPath.c_str(), "rb");
  if (!file_) {
    throw std::runtime_error("PostingsStreamReader: cannot open " +
                              postingsPath);
  }
  if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
    std::fclose(file_);
    throw std::runtime_error("PostingsStreamReader: seek failed");
  }
  loadCurrent();
}

PostingsStreamReader::~PostingsStreamReader() {
  if (file_) std::fclose(file_);
}

void PostingsStreamReader::loadCurrent() {
  if (remaining_ == 0) {
    currentLoaded_ = false;
    return;
  }
  currentDocId_ = readU32(file_);
  currentTermFreq_ = readU32(file_);
  currentPositions_.resize(currentTermFreq_);
  for (uint32_t i = 0; i < currentTermFreq_; ++i) {
    currentPositions_[i] = readU32(file_);
  }
  --remaining_;
  currentLoaded_ = true;
}

void PostingsStreamReader::next() {
  currentLoaded_ = false;
  loadCurrent();
}

void PostingsStreamReader::seek(DocId target) {
  while (valid() && currentDocId_ < target) next();
}

// ---------------- IndexReader ----------------

IndexReader::IndexReader(const std::string& pathPrefix) {
  postingsPath_ = pathPrefix + ".post";

  {
    FILE* f = std::fopen((pathPrefix + ".docs").c_str(), "rb");
    if (!f) throw std::runtime_error("IndexReader: cannot open .docs file");
    uint32_t magic = readU32(f);
    if (magic != kDocsMagic) {
      std::fclose(f);
      throw std::runtime_error("IndexReader: bad .docs magic");
    }
    readU32(f);  // version
    uint32_t numDocs = readU32(f);
    documents_.reserve(numDocs);
    for (uint32_t i = 0; i < numDocs; ++i) {
      DocumentMeta doc;
      doc.id = readU32(f);
      doc.url = readString(f);
      doc.path = readString(f);
      doc.title = readString(f);
      doc.length = readU32(f);
      doc.staticScore = readDouble(f);
      uint32_t numOut = readU32(f);
      doc.outlinks.reserve(numOut);
      for (uint32_t j = 0; j < numOut; ++j) doc.outlinks.push_back(readU32(f));
      docIndexById_[doc.id] = documents_.size();
      documents_.push_back(std::move(doc));
    }
    std::fclose(f);
  }

  {
    FILE* f = std::fopen((pathPrefix + ".lex").c_str(), "rb");
    if (!f) throw std::runtime_error("IndexReader: cannot open .lex file");
    uint32_t magic = readU32(f);
    if (magic != kLexMagic) {
      std::fclose(f);
      throw std::runtime_error("IndexReader: bad .lex magic");
    }
    readU32(f);  // version
    uint32_t numWords = readU32(f);
    lexicon_.reserve(numWords);
    for (uint32_t i = 0; i < numWords; ++i) {
      std::string word = readString(f);
      LexiconEntry entry;
      entry.offset = readU64(f);
      entry.count = readU32(f);
      lexicon_[word] = entry;
    }
    std::fclose(f);
  }

  // Optional PageRank sidecar (see pagerank.h): if present and sized to
  // match this document table, fold it into each doc's static score.
  std::vector<double> pagerankScores;
  if (PageRank::loadScores(pathPrefix, pagerankScores) &&
      pagerankScores.size() == documents_.size()) {
    for (size_t i = 0; i < documents_.size(); ++i) {
      documents_[i].staticScore = pagerankScores[i];
    }
  }
}

const DocumentMeta* IndexReader::getDocument(DocId id) const {
  auto it = docIndexById_.find(id);
  if (it == docIndexById_.end()) return nullptr;
  return &documents_[it->second];
}

uint32_t IndexReader::documentFrequency(const std::string& word) const {
  auto it = lexicon_.find(word);
  return it == lexicon_.end() ? 0 : it->second.count;
}

std::unique_ptr<PostingsStreamReader> IndexReader::openPostings(
    const std::string& word) const {
  auto it = lexicon_.find(word);
  if (it == lexicon_.end()) return nullptr;
  return std::make_unique<PostingsStreamReader>(postingsPath_, it->second.offset,
                                                 it->second.count);
}

}  // namespace engine
