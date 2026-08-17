#include <string>

#include "engine/hashtable.h"
#include "test_framework.h"

using namespace engine;

TEST_MAIN_BEGIN()
  HashTable<std::string, int> table(4);
  CHECK_EQ(table.size(), (size_t)0);

  table.getOrInsert("apple") = 1;
  table.getOrInsert("banana") = 2;
  table.getOrInsert("apple") += 10;  // second lookup mutates in place

  CHECK_EQ(table.size(), (size_t)2);
  CHECK_EQ(*table.find("apple"), 11);
  CHECK_EQ(*table.find("banana"), 2);
  CHECK(table.find("cherry") == nullptr);

  // force growth past the initial 4 buckets and confirm nothing is lost
  for (int i = 0; i < 500; ++i) {
    table.getOrInsert("key" + std::to_string(i)) = i;
  }
  CHECK_EQ(table.size(), (size_t)502);
  for (int i = 0; i < 500; ++i) {
    int* v = table.find("key" + std::to_string(i));
    CHECK(v != nullptr);
    if (v) CHECK_EQ(*v, i);
  }
  CHECK_EQ(*table.find("apple"), 11);

  size_t visited = 0;
  table.forEach([&](const std::string&, int) { ++visited; });
  CHECK_EQ(visited, table.size());
TEST_MAIN_END()
