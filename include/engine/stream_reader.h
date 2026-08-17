#pragma once
// Level 5: the constraint solver. Every operator (AND / OR / NOT / PHRASE)
// is a StreamReader that walks a sorted stream of DocIds and can be
// composed with the others into a tree; QueryParser compiles a query
// string into exactly such a tree, and evaluating the root's valid()/
// next() pair enumerates the whole result set via merge-join, without
// ever materializing an intermediate "all docs matching word X" list in
// full (each leaf streams lazily from disk through PostingsStreamReader).
//
// This class hierarchy intentionally answers only *set membership*
// ("which documents match this boolean query"), not ranking -- Ranker
// re-scores the resulting DocIds separately (Level 6), which mirrors how
// production search engines split boolean filtering from relevance
// scoring into separate passes.

#include <memory>
#include <string>
#include <vector>

#include "engine/common.h"
#include "engine/index_reader.h"

namespace engine {

class StreamReader {
 public:
  virtual ~StreamReader() = default;
  virtual bool valid() const = 0;
  virtual DocId docId() const = 0;
  virtual void next() = 0;
  // Advances until docId() >= target or the stream is exhausted.
  virtual void seek(DocId target) = 0;
};

// Leaf node: wraps a single word's on-disk postings.
class TermStream : public StreamReader {
 public:
  explicit TermStream(std::unique_ptr<PostingsStreamReader> reader);

  bool valid() const override;
  DocId docId() const override;
  void next() override;
  void seek(DocId target) override;

  uint32_t termFreq() const;
  const std::vector<uint32_t>& positions() const;

 private:
  std::unique_ptr<PostingsStreamReader> reader_;
};

// Intersection: matches documents present in every child stream.
class AndStream : public StreamReader {
 public:
  explicit AndStream(std::vector<std::unique_ptr<StreamReader>> children);

  bool valid() const override;
  DocId docId() const override;
  void next() override;
  void seek(DocId target) override;

 private:
  void advanceToMatch();

  std::vector<std::unique_ptr<StreamReader>> children_;
  bool valid_ = false;
};

// Union: matches documents present in at least one child stream.
class OrStream : public StreamReader {
 public:
  explicit OrStream(std::vector<std::unique_ptr<StreamReader>> children);

  bool valid() const override;
  DocId docId() const override;
  void next() override;
  void seek(DocId target) override;

 private:
  void recomputeMin();

  std::vector<std::unique_ptr<StreamReader>> children_;
  DocId currentMin_ = kInvalidDocId;
  bool valid_ = false;
};

// Difference: matches documents in `base` that are absent from `excluded`.
class NotStream : public StreamReader {
 public:
  NotStream(std::unique_ptr<StreamReader> base,
            std::unique_ptr<StreamReader> excluded);

  bool valid() const override;
  DocId docId() const override;
  void next() override;
  void seek(DocId target) override;

 private:
  void skipExcluded();

  std::unique_ptr<StreamReader> base_;
  std::unique_ptr<StreamReader> excluded_;
};

// Phrase match: requires every word to appear in the document at
// consecutive token positions, in the given order.
class PhraseStream : public StreamReader {
 public:
  explicit PhraseStream(std::vector<std::unique_ptr<TermStream>> children);

  bool valid() const override;
  DocId docId() const override;
  void next() override;
  void seek(DocId target) override;

 private:
  void advanceToPhraseMatch();
  bool phraseMatchesAtCurrentDoc() const;

  std::vector<std::unique_ptr<TermStream>> children_;
  bool valid_ = false;
};

}  // namespace engine
