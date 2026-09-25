import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"
#include "scan_shapes.h"

namespace {

TEST(Scan, BracesAreTextAndPlacesGoWhereParseSays) {
  const auto got = read<pair_ab>(R"({"a":5,"b":"xy"})");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_EQ(got->value.a, 5);
  EXPECT_EQ(got->value.b, "xy");
}

}  // namespace
