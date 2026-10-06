import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

// Read once, a character at a time, from something that is not text in
// memory: a list, and a view that joins pieces as they would arrive.
TEST(Read, AnInputRange) {
  const std::string text = R"({"content":"hi","depth":3,"prev":["$a"]})";
  const std::list<char> letters(text.begin(), text.end());
  const auto got = strict<event>(letters);
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->content, "hi");

  const std::vector<std::string> pieces{R"({"content":"h)", R"(i","depth")",
                                        R"(:3,"prev":["$a"]})"};
  const auto joined = strict<event>(std::views::join(pieces));
  ASSERT_TRUE(joined) << joined.error().message;
  EXPECT_EQ(joined->prev, (std::vector<std::string>{"$a"}));
}

}  // namespace
