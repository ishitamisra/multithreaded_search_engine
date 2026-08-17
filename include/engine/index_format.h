#pragma once
// On-disk layout for the reverse index (Level 3: "define a file format").
//
// An index with prefix `foo` is three files:
//
//   foo.docs   Document table. Header + one fixed/variable record per
//              document (id, url, path, title, token length, static
//              score, outlink ids). Small enough to load fully into RAM.
//
//   foo.lex    Lexicon. Header + one record per distinct word: the word
//              itself, plus the byte offset and posting count of its
//              postings run inside foo.post. Loaded fully into RAM (this
//              is the classic "keep the dictionary in memory, stream the
//              postings from disk" search-engine design).
//
//   foo.post   Postings. For each word (in lexicon order), its postings
//              sorted by ascending DocId: docId, termFreq, then termFreq
//              position ints. Read via IndexReader::openPostings(), which
//              streams sequentially rather than loading the whole file.
//
// All integers are little-endian, written with raw fwrite (this project
// targets x86/ARM little-endian dev machines, so no byte-swapping layer).

#include <cstdint>
#include <cstdio>
#include <string>

namespace engine {

constexpr uint32_t kDocsMagic = 0x53454458;   // "SEDX"
constexpr uint32_t kLexMagic = 0x53454C58;    // "SELX"
constexpr uint32_t kPostMagic = 0x53455058;   // "SEPX"
constexpr uint32_t kIndexFormatVersion = 1;

// --- Low-level binary I/O helpers shared by writer/reader ---
void writeU32(FILE* f, uint32_t v);
void writeU64(FILE* f, uint64_t v);
void writeDouble(FILE* f, double v);
void writeString(FILE* f, const std::string& s);

uint32_t readU32(FILE* f);
uint64_t readU64(FILE* f);
double readDouble(FILE* f);
std::string readString(FILE* f);

}  // namespace engine
