import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, AnEmptyObject) {
  EXPECT_EQ(json(shapes::nothing{}), "{}");
}

}  // namespace
