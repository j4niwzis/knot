import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, UnicodeIsTakenAsItIs) {
  const auto got = strict<event>(
      "{\"content\":\"\xd0\xbf\xe2\x82\xac\xf0\x9f\x98\x80\",\"depth\":-5,"
      "\"prev\":[]}");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->content, "\xd0\xbf\xe2\x82\xac\xf0\x9f\x98\x80");
  EXPECT_EQ(got->depth, -5);
}

}  // namespace
