import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, ReadBack) {
  const shapes::content one{{"$e\n", "r"}, "\x01\"", false, {{}, {65535}}};
  const auto back = knot::read<shapes::content>(json(one));
  ASSERT_TRUE(back) << back.error().message << " at " << back.error().offset;
  EXPECT_EQ(*back, one);
}

}  // namespace
