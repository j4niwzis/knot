import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Pattern, AnArrayForAFoldHasAGroupAnElement) {
  EXPECT_EQ(knot::detail::array_groups<std::int64_t>.view(),
            R"(\[(?:()" + integer + ")(?:,(" + integer + R"())*)?\])");
}

}  // namespace
