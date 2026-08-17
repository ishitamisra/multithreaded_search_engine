#include "engine/stream_reader.h"

#include <algorithm>

namespace engine {

// ---------------- TermStream ----------------

TermStream::TermStream(std::unique_ptr<PostingsStreamReader> reader)
    : reader_(std::move(reader)) {}

bool TermStream::valid() const { return reader_ && reader_->valid(); }
DocId TermStream::docId() const { return reader_->docId(); }
void TermStream::next() { reader_->next(); }
void TermStream::seek(DocId target) { reader_->seek(target); }
uint32_t TermStream::termFreq() const { return reader_->termFreq(); }
const std::vector<uint32_t>& TermStream::positions() const {
  return reader_->positions();
}

// ---------------- AndStream ----------------

AndStream::AndStream(std::vector<std::unique_ptr<StreamReader>> children)
    : children_(std::move(children)) {
  advanceToMatch();
}

void AndStream::advanceToMatch() {
  valid_ = false;
  if (children_.empty()) return;
  for (;;) {
    for (auto& c : children_) {
      if (!c->valid()) return;
    }
    DocId maxId = 0;
    for (auto& c : children_) maxId = std::max(maxId, c->docId());
    bool allEqual = true;
    for (auto& c : children_) {
      if (c->docId() != maxId) {
        c->seek(maxId);
        allEqual = false;
      }
    }
    if (allEqual) {
      valid_ = true;
      return;
    }
  }
}

bool AndStream::valid() const { return valid_; }
DocId AndStream::docId() const { return children_.front()->docId(); }

void AndStream::next() {
  for (auto& c : children_) c->next();
  advanceToMatch();
}

void AndStream::seek(DocId target) {
  for (auto& c : children_) c->seek(target);
  advanceToMatch();
}

// ---------------- OrStream ----------------

OrStream::OrStream(std::vector<std::unique_ptr<StreamReader>> children)
    : children_(std::move(children)) {
  recomputeMin();
}

void OrStream::recomputeMin() {
  currentMin_ = kInvalidDocId;
  valid_ = false;
  for (auto& c : children_) {
    if (c->valid() && c->docId() < currentMin_) {
      currentMin_ = c->docId();
      valid_ = true;
    }
  }
}

bool OrStream::valid() const { return valid_; }
DocId OrStream::docId() const { return currentMin_; }

void OrStream::next() {
  for (auto& c : children_) {
    if (c->valid() && c->docId() == currentMin_) c->next();
  }
  recomputeMin();
}

void OrStream::seek(DocId target) {
  for (auto& c : children_) {
    if (c->valid()) c->seek(target);
  }
  recomputeMin();
}

// ---------------- NotStream ----------------

NotStream::NotStream(std::unique_ptr<StreamReader> base,
                      std::unique_ptr<StreamReader> excluded)
    : base_(std::move(base)), excluded_(std::move(excluded)) {
  skipExcluded();
}

void NotStream::skipExcluded() {
  while (base_->valid()) {
    excluded_->seek(base_->docId());
    if (excluded_->valid() && excluded_->docId() == base_->docId()) {
      base_->next();
    } else {
      return;
    }
  }
}

bool NotStream::valid() const { return base_->valid(); }
DocId NotStream::docId() const { return base_->docId(); }

void NotStream::next() {
  base_->next();
  skipExcluded();
}

void NotStream::seek(DocId target) {
  base_->seek(target);
  skipExcluded();
}

// ---------------- PhraseStream ----------------

PhraseStream::PhraseStream(std::vector<std::unique_ptr<TermStream>> children)
    : children_(std::move(children)) {
  advanceToPhraseMatch();
}

bool PhraseStream::phraseMatchesAtCurrentDoc() const {
  if (children_.empty()) return false;
  const auto& firstPositions = children_.front()->positions();
  for (uint32_t start : firstPositions) {
    bool ok = true;
    for (size_t i = 1; i < children_.size(); ++i) {
      const auto& positions = children_[i]->positions();
      if (!std::binary_search(positions.begin(), positions.end(),
                               start + static_cast<uint32_t>(i))) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

void PhraseStream::advanceToPhraseMatch() {
  valid_ = false;
  if (children_.empty()) return;
  for (;;) {
    for (auto& c : children_) {
      if (!c->valid()) return;
    }
    DocId maxId = 0;
    for (auto& c : children_) maxId = std::max(maxId, c->docId());
    bool allEqual = true;
    for (auto& c : children_) {
      if (c->docId() != maxId) {
        c->seek(maxId);
        allEqual = false;
      }
    }
    if (!allEqual) continue;
    if (phraseMatchesAtCurrentDoc()) {
      valid_ = true;
      return;
    }
    for (auto& c : children_) c->next();
  }
}

bool PhraseStream::valid() const { return valid_; }
DocId PhraseStream::docId() const { return children_.front()->docId(); }

void PhraseStream::next() {
  for (auto& c : children_) c->next();
  advanceToPhraseMatch();
}

void PhraseStream::seek(DocId target) {
  for (auto& c : children_) c->seek(target);
  advanceToPhraseMatch();
}

}  // namespace engine
