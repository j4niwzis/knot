import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Format, AnEmptyObject) {
  EXPECT_EQ(knot::scan_format<shapes::nothing>.view(), R"(\{\})"sv);
  EXPECT_EQ(knot::pattern<shapes::nothing>.view(), R"(\{\})"sv);
}

}  // namespace
