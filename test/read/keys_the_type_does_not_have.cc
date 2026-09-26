import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(ReadBoth, KeysTheTypeDoesNotHaveArePassedOver) {
  const auto got = knot::try_read<event>(
      R"({"x":{"a":[1,{"b":null}],"c":-1.5e3},"content":"hi","depth":2,)"
      R"("prev":[],"z":"\u00e9\n"})");
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->content, "hi");

  const auto canonical = strict<event>(
      R"({"a":[true,null,{"x":"y"}],"content":"hi","depth":2,"prev":[],"z":1})");
  ASSERT_TRUE(canonical) << canonical.error().message;
  EXPECT_EQ(canonical->depth, 2);
  // Passed over, but still Canonical JSON: no floats, keys in order inside.
  EXPECT_FALSE(strict<event>(R"({"a":1.5,"content":"","depth":0,"prev":[]})"));
  EXPECT_FALSE(strict<event>(R"({"a":{"y":1,"x":2},"content":"","depth":0,"prev":[]})"));
}

}  // namespace
