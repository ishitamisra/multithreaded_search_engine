#include "engine/index_format.h"

#include <stdexcept>
#include <vector>

namespace engine {

namespace {
void checkedWrite(const void* ptr, size_t size, FILE* f) {
  if (std::fwrite(ptr, size, 1, f) != 1) {
    throw std::runtime_error("index_format: write failed (disk full?)");
  }
}
void checkedRead(void* ptr, size_t size, FILE* f) {
  if (std::fread(ptr, size, 1, f) != 1) {
    throw std::runtime_error("index_format: unexpected EOF / read failed");
  }
}
}  // namespace

void writeU32(FILE* f, uint32_t v) { checkedWrite(&v, sizeof(v), f); }
void writeU64(FILE* f, uint64_t v) { checkedWrite(&v, sizeof(v), f); }
void writeDouble(FILE* f, double v) { checkedWrite(&v, sizeof(v), f); }

void writeString(FILE* f, const std::string& s) {
  writeU32(f, static_cast<uint32_t>(s.size()));
  if (!s.empty()) checkedWrite(s.data(), s.size(), f);
}

uint32_t readU32(FILE* f) {
  uint32_t v;
  checkedRead(&v, sizeof(v), f);
  return v;
}
uint64_t readU64(FILE* f) {
  uint64_t v;
  checkedRead(&v, sizeof(v), f);
  return v;
}
double readDouble(FILE* f) {
  double v;
  checkedRead(&v, sizeof(v), f);
  return v;
}
std::string readString(FILE* f) {
  uint32_t len = readU32(f);
  std::string s(len, '\0');
  if (len > 0) checkedRead(s.data(), len, f);
  return s;
}

}  // namespace engine
