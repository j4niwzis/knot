import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"
#include "scan_shapes.h"

namespace {

TEST(Scan, ExactlyTheTextAndNothingElse) {
  EXPECT_FALSE(read<pair_ab>(R"({"a": 5,"b":"xy"})"));
  EXPECT_FALSE(read<pair_ab>(R"({"b":"xy","a":5})"));
  EXPECT_FALSE(read<pair_ab>(R"({"a":5,"b":"xy"} )"));
  EXPECT_FALSE(read<pair_ab>(R"({"a":5,"b":"xy")"));
}

}  // namespace
