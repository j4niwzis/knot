import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

// Formatted as JSON: std::format writes it through its own output.
TEST(Write, Formatted) {
  const event value{"formatted", 3, {"$c"}};
  EXPECT_EQ(std::format("{}", knot::as_json{value}), knot::to_json_string(value));
  EXPECT_EQ(std::format("[{}]", knot::as_json{value}), "[" + knot::to_json_string(value) + "]");
}

}  // namespace
