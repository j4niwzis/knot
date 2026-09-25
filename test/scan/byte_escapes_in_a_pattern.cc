import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"
#include "scan_shapes.h"

namespace {

TEST(Scan, ByteEscapesInAPattern) {
  const auto got = read<latin>("\"caf\xc3\xa9\"");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_EQ(got->value.text, "caf\xc3\xa9");
  EXPECT_FALSE(read<latin>("\"caf\xc3\""));         // cut short
  EXPECT_FALSE(read<latin>("\"\xc0\x80\""));        // overlong
  EXPECT_FALSE(read<latin>("\"a\"b\""));            // a quote inside
}

}  // namespace
