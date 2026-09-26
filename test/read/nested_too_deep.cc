import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(ReadBoth, WhatIsPassedOverMayNotNestForever) {
  const std::string deep = std::string(200, '[') + std::string(200, ']');
  const std::string text = R"({"a":)" + deep + R"(,"content":"","depth":0,"prev":[]})";
  EXPECT_FALSE(knot::try_read<event>(text));
  EXPECT_FALSE(strict<event>(text));
  const std::string fine = std::string(100, '[') + std::string(100, ']');
  EXPECT_TRUE(knot::try_read<event>(R"({"a":)" + fine + R"(,"content":"","depth":0,"prev":[]})"));
}

}  // namespace
