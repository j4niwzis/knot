import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"
#include "scan_shapes.h"

namespace {

TEST(Scan, AShapeInsideAShape) {
  const auto got = read<outer>(R"({"inner":{"a":1,"b":"q"},"n":7})");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_EQ(got->value.inner.a, 1);
  EXPECT_EQ(got->value.inner.b, "q");
  EXPECT_EQ(got->value.n, 7);
  EXPECT_FALSE(read<outer>(R"({"inner":{"a":1,"b":"q"}},"n":7})"));
}

}  // namespace
