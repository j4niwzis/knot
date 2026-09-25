import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

// A range that can be read once and only once: characters out of a stream.
TEST(Read, FromAStream) {
  std::istringstream text(
      R"({ "prev": ["$a"], "depth": 3, "content": "hi\n", "extra": {"x": [1, 2.5]} })");
  const auto got = knot::read<event>(std::ranges::subrange(
      std::istreambuf_iterator<char>(text), std::istreambuf_iterator<char>()));
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->content, "hi\n");
  EXPECT_EQ(got->depth, 3);
  EXPECT_EQ(got->prev, (std::vector<std::string>{"$a"}));

  std::istringstream broken(R"({"content":"hi","depth":01,"prev":[]})");
  const auto refused = knot::read<event>(std::ranges::subrange(
      std::istreambuf_iterator<char>(broken), std::istreambuf_iterator<char>()));
  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error().offset, 25u);  // the 1 after the 0
}

}  // namespace
