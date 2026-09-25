import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Format, KeysAreSorted) {
  EXPECT_EQ(knot::scan_format<shapes::unsorted>.view(),
            R"(\{"apple":{},"mango":{},"zebra":{}\})"sv);
  constexpr auto order = knot::order_of<shapes::unsorted>;
  EXPECT_EQ(order, (std::array<std::size_t, 3>{1, 2, 0}));
}

}  // namespace
