import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Format, KeysAreEscapedForJsonAndThenForScan) {
  // JSON: "a{b}\"c\n"; each character of that as itself.
  EXPECT_EQ(knot::scan_format<shapes::awkward>.view(),
            R"(\{"a\{b\}\\"c\\n":{},"plain":{}\})"sv);
}

}  // namespace
