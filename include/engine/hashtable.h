#pragma once
// A hand-rolled separate-chaining hash table, used as the in-memory
// structure that Level 1 of the assignment calls for ("parse text files
// into a hash table"). The indexer uses HashTable<std::string,
// PostingsList> to accumulate word -> per-document occurrence lists while
// walking a corpus, before the whole thing is flushed to the on-disk
// index format.
//
// Not a drop-in std::unordered_map replacement: no iterator invalidation
// guarantees, no erase(). It supports exactly what the indexer needs:
// insert-or-get-mutable-reference, lookup, size, and ordered-by-bucket
// iteration (forEach), with automatic growth to keep the load factor low.

#include <cstddef>
#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace engine {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class HashTable {
 public:
  explicit HashTable(size_t initialBuckets = 1024)
      : buckets_(initialBuckets == 0 ? 1 : initialBuckets) {}

  // Returns a reference to the value for `key`, default-constructing one
  // if the key is not already present. Reference stays valid until the
  // next insert that triggers a resize.
  Value& getOrInsert(const Key& key) {
    if (loadFactor() > kMaxLoadFactor) rehash(buckets_.size() * 2);
    size_t idx = bucketIndex(key);
    for (auto& entry : buckets_[idx]) {
      if (entry.first == key) return entry.second;
    }
    buckets_[idx].emplace_back(key, Value{});
    ++size_;
    return buckets_[idx].back().second;
  }

  Value* find(const Key& key) {
    size_t idx = bucketIndex(key);
    for (auto& entry : buckets_[idx]) {
      if (entry.first == key) return &entry.second;
    }
    return nullptr;
  }

  const Value* find(const Key& key) const {
    size_t idx = bucketIndex(key);
    for (const auto& entry : buckets_[idx]) {
      if (entry.first == key) return &entry.second;
    }
    return nullptr;
  }

  size_t size() const { return size_; }
  size_t bucketCount() const { return buckets_.size(); }
  double loadFactor() const {
    return static_cast<double>(size_) / static_cast<double>(buckets_.size());
  }

  // Visits every (key, value) pair. Order is bucket order, not insertion
  // order (this is a hash table, not an association list) but it is
  // deterministic for a fixed bucket count, which is what index_writer
  // relies on to produce a stable lexicon ordering pass before sorting.
  template <typename Fn>
  void forEach(Fn&& fn) const {
    for (const auto& bucket : buckets_) {
      for (const auto& entry : bucket) fn(entry.first, entry.second);
    }
  }

  template <typename Fn>
  void forEachMutable(Fn&& fn) {
    for (auto& bucket : buckets_) {
      for (auto& entry : bucket) fn(entry.first, entry.second);
    }
  }

 private:
  static constexpr double kMaxLoadFactor = 2.0;

  size_t bucketIndex(const Key& key) const {
    return Hash{}(key) % buckets_.size();
  }

  void rehash(size_t newBucketCount) {
    std::vector<std::list<std::pair<Key, Value>>> newBuckets(newBucketCount);
    for (auto& bucket : buckets_) {
      for (auto& entry : bucket) {
        size_t idx = Hash{}(entry.first) % newBucketCount;
        newBuckets[idx].push_back(std::move(entry));
      }
    }
    buckets_ = std::move(newBuckets);
  }

  std::vector<std::list<std::pair<Key, Value>>> buckets_;
  size_t size_ = 0;
};

}  // namespace engine
