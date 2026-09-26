import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

struct joined { static constexpr std::string_view json_value = "join"; };
struct dotted { static constexpr std::string_view json_value = "m.read"; };

TEST(Pattern, Choices) {
  EXPECT_EQ((knot::pattern<std::variant<joined, dotted>>.view()),
            R"((?:"join"|"m\.read"))"sv);
  EXPECT_EQ((knot::pattern<std::variant<joined, std::string>>.view()),
            knot::patterns::string);
}

}  // namespace
