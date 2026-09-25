import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, EscapesAreDecoded) {
  const auto got = strict<event>(
      R"({"content":"a\"b\\c\nd\u0001\u001f\t","depth":0,"prev":[]})");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->content, "a\"b\\c\nd\x01\x1f\t");
  EXPECT_TRUE(got->prev.empty());
}

}  // namespace
