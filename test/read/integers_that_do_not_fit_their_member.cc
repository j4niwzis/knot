import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, IntegersThatDoNotFitTheirMember) {
  EXPECT_TRUE(strict<shapes::sizes>(R"({"count":4294967295,"small":-1})"));
  EXPECT_FALSE(strict<shapes::sizes>(R"({"count":-1,"small":0})"));
  EXPECT_FALSE(strict<shapes::sizes>(R"({"count":4294967296,"small":0})"));
  EXPECT_FALSE(strict<shapes::sizes>(R"({"count":0,"small":2147483648})"));
}

}  // namespace
