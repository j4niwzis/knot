import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Pattern, ArraysOfArrays) {
  const std::string row = R"(\[(?:)" + integer + "(?:," + integer + R"()*)?\])";
  EXPECT_EQ(knot::pattern<shapes::counts>.view(),
            R"(\{"grid":\[(?:)" + row + "(?:," + row + R"()*)?\],"seen":)" +
                std::string(knot::patterns::natural) + R"(\})");
}

}  // namespace
