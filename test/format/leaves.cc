import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Pattern, Leaves) {
  EXPECT_EQ(knot::pattern<std::string>.view(), knot::patterns::string);
  EXPECT_EQ(knot::pattern<bool>.view(), "(?:true|false)"sv);
  EXPECT_EQ(knot::pattern<std::int32_t>.view(), knot::patterns::integer);
  EXPECT_EQ(knot::pattern<std::uint64_t>.view(), knot::patterns::natural);
}

}  // namespace
