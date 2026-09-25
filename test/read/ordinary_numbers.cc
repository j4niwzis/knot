import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(ReadOrdinary, NumbersInAnySpellingThatIsWhole) {
  const auto depth = [](std::string_view number) -> std::optional<std::int64_t> {
    const auto got = knot::read<event>(R"({"content":"","depth":)" +
                                       std::string(number) + R"(,"prev":[]})");
    if (!got) return std::nullopt;
    return got->depth;
  };
  EXPECT_EQ(depth("1.0"), 1);
  EXPECT_EQ(depth("1e3"), 1000);
  EXPECT_EQ(depth("1E+3"), 1000);
  EXPECT_EQ(depth("10e-1"), 1);
  EXPECT_EQ(depth("0.25e2"), 25);
  EXPECT_EQ(depth("-0"), 0);
  EXPECT_EQ(depth("-0.0e7"), 0);
  EXPECT_EQ(depth("0e999999999"), 0);
  EXPECT_EQ(depth("9007199254740991"), 9007199254740991);
  EXPECT_EQ(depth("-9.007199254740991e15"), -9007199254740991);
  EXPECT_FALSE(depth("1.5"));
  EXPECT_FALSE(depth("1e-1"));
  EXPECT_FALSE(depth("9007199254740992"));
  EXPECT_FALSE(depth("1e16"));
  EXPECT_FALSE(depth("1e999999999"));
  EXPECT_FALSE(depth("01"));
  EXPECT_FALSE(depth("1."));
  EXPECT_FALSE(depth(".5"));
  EXPECT_FALSE(depth("+1"));
  EXPECT_FALSE(depth("1e"));
}

}  // namespace
