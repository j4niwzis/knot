import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Format, RenamedKeysAreSortedByTheirNewName) {
  // "body" < "m.relates_to", though relates_to is declared first.
  EXPECT_EQ(knot::scan_format<shapes::content>.view(),
            R"(\{"body":{},"m\.relates_to":{}\})"sv);
  EXPECT_EQ(knot::order_of<shapes::content>,
            (std::array<std::size_t, 2>{1, 0}));
}

}  // namespace
