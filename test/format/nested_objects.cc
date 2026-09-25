import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Pattern, NestedObjects) {
  EXPECT_EQ(knot::pattern<shapes::content>.view(),
            R"(\{"body":)" + string + R"(,"m\.relates_to":\{"event_id":)" +
                string + R"(,"rel_type":)" + string + R"(\}\})");
}

}  // namespace
