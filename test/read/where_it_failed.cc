import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, WhereItFailed) {
  const auto got = knot::read<event>(R"({"content":"","depth":01,"prev":[]})");
  ASSERT_FALSE(got);
  EXPECT_EQ(got.error().offset, 23u);  // the '1' after the '0'
  const auto keys = knot::read<event>(R"({"depth":0,"content":"","prev":[]})");
  ASSERT_FALSE(keys);
  EXPECT_EQ(keys.error().offset, 2u);  // "d" where "c" belongs
  const auto after = knot::read<event>(R"({"content":"","depth":0,"prev":[]}x)");
  ASSERT_FALSE(after);
  EXPECT_EQ(after.error().offset, 34u);  // the x
}

}  // namespace
