import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, AnEvent) {
  const auto got =
      strict<event>(R"({"content":"hi","depth":3,"prev":["$a","$b"]})");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->content, "hi");
  EXPECT_EQ(got->depth, 3);
  EXPECT_EQ(got->prev, (std::vector<std::string>{"$a", "$b"}));
}

}  // namespace
