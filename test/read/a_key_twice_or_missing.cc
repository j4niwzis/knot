import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(ReadBoth, AKeyTwiceOrMissing) {
  EXPECT_FALSE(knot::read<event>(R"({"content":"a","content":"b","depth":0,"prev":[]})"));
  EXPECT_FALSE(knot::read<event>(R"({"content":"a","prev":[]})"));
  EXPECT_FALSE(strict<event>(R"({"content":"a","content":"b","depth":0,"prev":[]})"));
}

}  // namespace
