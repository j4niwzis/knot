import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

// Where an unknown key sorts is where it left the trie of the known ones.
TEST(ReadCanonical, UnknownKeysInTheirPlace) {
  const auto with = [](std::string_view before, std::string_view after) {
    return strict<event>(std::string("{") + std::string(before) +
                         R"("content":"","depth":0,"prev":[])" +
                         std::string(after) + "}");
  };
  // Before, between and after the known keys.
  EXPECT_TRUE(with(R"("a":1,)", ""));
  EXPECT_TRUE(with("", R"(,"q":1)"));
  EXPECT_TRUE(with(R"("co":1,"conten":2,)", ""));      // prefixes of "content"
  EXPECT_TRUE(with("", R"(,"prevs":1,"z":2)"));         // "prev" is a prefix of it
  EXPECT_TRUE(strict<event>(R"({"content":"","d":1,"depth":0,"e":2,"prev":[]})"));
  // Two unknown keys in one gap: told apart by their text.
  EXPECT_TRUE(with(R"("a":1,"b":2,)", ""));
  EXPECT_FALSE(with(R"("b":1,"a":2,)", ""));
  EXPECT_FALSE(with(R"("a":1,"a":2,)", ""));
  // Out of place against a known key.
  EXPECT_FALSE(with("", R"(,"a":1)"));
  EXPECT_FALSE(with("", R"(,"conten":1)"));
  EXPECT_FALSE(strict<event>(R"({"content":"","depth":0,"d":1,"prev":[]})"));
}

}  // namespace
