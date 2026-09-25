import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, WhereItFailed) {
  const auto got = strict<event>(R"({"content":"","depth":01,"prev":[]})");
  ASSERT_FALSE(got);
  EXPECT_EQ(got.error().offset, 23u);  // the '1' after the '0'
  const auto keys = strict<event>(R"({"depth":0,"content":"","prev":[]})");
  ASSERT_FALSE(keys);
  EXPECT_EQ(keys.error().offset, 11u);  // "content" after "depth"
  const auto after = strict<event>(R"({"content":"","depth":0,"prev":[]}x)");
  ASSERT_FALSE(after);
  EXPECT_EQ(after.error().offset, 34u);  // the x
}

}  // namespace
