#pragma once
// A deliberately tiny test harness: no external dependency, just enough
// to write "run these checks, print a summary, exit non-zero on failure"
// unit tests that ctest can drive.

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace testkit {
inline int& failureCount() {
  static int count = 0;
  return count;
}
inline int& checkCount() {
  static int count = 0;
  return count;
}
}  // namespace testkit

#define CHECK(cond)                                                      \
  do {                                                                    \
    ++testkit::checkCount();                                              \
    if (!(cond)) {                                                        \
      ++testkit::failureCount();                                          \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond \
                << "\n";                                                  \
    }                                                                     \
  } while (0)

// Deliberately does not print the compared values: some test fixtures
// compare containers (std::set<DocId>, etc.) that have no operator<<, and
// requiring one from every value type this macro is used with would
// defeat the point of a lightweight harness. The failing expression text
// plus file:line is enough to locate the mismatch.
#define CHECK_EQ(a, b)                                                    \
  do {                                                                    \
    ++testkit::checkCount();                                              \
    if (!((a) == (b))) {                                                  \
      ++testkit::failureCount();                                          \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a    \
                << " == " << #b << "\n";                                   \
    }                                                                     \
  } while (0)

#define TEST_MAIN_BEGIN() int main() {
#define TEST_MAIN_END()                                                   \
  std::cerr << (testkit::checkCount() - testkit::failureCount()) << "/"    \
            << testkit::checkCount() << " checks passed\n";                \
  return testkit::failureCount() == 0 ? 0 : 1;                             \
  }
