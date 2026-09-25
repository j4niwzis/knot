import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"
#include "scan_shapes.h"

namespace {

TEST(Scan, AnArrayByAFoldIsExact) {
  const auto empty = read<numbers>("[]");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->value.values.empty());

  const auto three = read<numbers>("[1,22,333]");
  ASSERT_TRUE(three);
  EXPECT_EQ(three->value.values, (std::vector<int>{1, 22, 333}));

  const auto one = read<numbers>("[12]");
  ASSERT_TRUE(one);
  EXPECT_EQ(one->value.values, (std::vector<int>{12}));

  EXPECT_FALSE(read<numbers>("[1,]"));
  EXPECT_FALSE(read<numbers>("[,1]"));
  EXPECT_FALSE(read<numbers>("[1,,2]"));
  EXPECT_FALSE(read<numbers>("[1 2]"));
}

}  // namespace
