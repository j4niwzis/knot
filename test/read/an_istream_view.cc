import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

// Read from std::views::istream<char>: a stream's characters as a view,
// each taken with operator>> -- so with whitespace skipping off, or the
// spaces inside a string would be lost.
TEST(Read, AnIstreamView) {
  std::istringstream text(R"({ "prev": ["$a", "$b"], "depth": 4, "content": "two words\n" })");
  text >> std::noskipws;
  const auto got = knot::try_read<event>(std::views::istream<char>(text));
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->content, "two words\n");
  EXPECT_EQ(got->depth, 4);
  EXPECT_EQ(got->prev, (std::vector<std::string>{"$a", "$b"}));
}

// Skipping whitespace is the stream's choice: what it leaves is read as
// it is -- a string's spaces gone, and the JSON still whole.
TEST(Read, AnIstreamViewSkippingSpaces) {
  std::istringstream text(R"({"content": "a b", "depth": 1, "prev": []})");
  const auto got = knot::try_read<event>(std::views::istream<char>(text));
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->content, "ab");
}

// One that is not JSON: refused, where it went wrong.
TEST(Read, AnIstreamViewRefused) {
  std::istringstream text(R"({"content":"x","depth":01,"prev":[]})");
  text >> std::noskipws;
  const auto refused = knot::try_read<event>(std::views::istream<char>(text));
  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error().offset, 24u);  // the 1 after the 0
}

}  // namespace
