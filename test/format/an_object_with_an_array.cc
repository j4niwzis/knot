import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Pattern, AnObjectWithAnArray) {
  EXPECT_EQ(knot::pattern<shapes::event>.view(),
            R"(\{"content":)" + string + R"(,"depth":)" + integer +
                R"(,"prev":\[(?:)" + string + "(?:," + string +
                R"()*)?\]\})");
}

}  // namespace
