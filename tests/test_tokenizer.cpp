#include "engine/tokenizer.h"
#include "test_framework.h"

using namespace engine;

TEST_MAIN_BEGIN()
  Tokenizer basic;
  auto tokens = basic.tokenize("The Quick-Brown Fox jumps over 2 lazy dogs!");
  CHECK_EQ(tokens.size(), (size_t)9);
  CHECK_EQ(tokens[0], std::string("the"));
  CHECK_EQ(tokens[1], std::string("quick"));
  CHECK_EQ(tokens[2], std::string("brown"));
  CHECK_EQ(tokens[8], std::string("dogs"));

  Tokenizer noStop(TokenizerOptions{true, false, 1});
  auto filtered = noStop.tokenize("the cat sat on the mat");
  for (const auto& t : filtered) CHECK(t != "the" && t != "on");

  CHECK_EQ(Tokenizer::stem("running"), std::string("runn"));
  CHECK_EQ(Tokenizer::toLower("MixedCase"), std::string("mixedcase"));

  Tokenizer offsetTok;
  auto spans = offsetTok.tokenizeWithOffsets("Hello, World!");
  CHECK_EQ(spans.size(), (size_t)2);
  CHECK_EQ(spans[0].word, std::string("hello"));
  CHECK_EQ(spans[0].start, (size_t)0);
  CHECK_EQ(spans[0].end, (size_t)5);
  CHECK_EQ(spans[1].word, std::string("world"));
TEST_MAIN_END()
