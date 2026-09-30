// SPDX-License-Identifier: AGPL-3.0-only
// A choice held in a splice::variant: read and written as a std::variant one is.
import std;
import splice;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace splice_choices {

struct values {
  struct join {
    static constexpr std::string_view json_value = "join";
  };
  struct leave {
    static constexpr std::string_view json_value = "leave";
  };
};
using membership_t = splice::variant<values::join, values::leave, std::string>;

struct member {
  membership_t membership;
};
consteval auto json_schema(knot::type<member>) { return knot::schema<member>(); }

}  // namespace splice_choices

using namespace splice_choices;

TEST(SpliceChoice, ReadsANamedOne) {
  const auto got = knot::try_read<member>(R"({"membership":"leave"})");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->membership.index(), 1u);
}

TEST(SpliceChoice, KeepsAnUnknownOne) {
  const auto got = knot::try_read<member>(R"({"membership":"knock"})");
  ASSERT_TRUE(got.has_value());
  ASSERT_EQ(got->membership.index(), 2u);
  EXPECT_EQ(splice::get<std::string>(got->membership), "knock");
}

TEST(SpliceChoice, WritesBack) {
  EXPECT_EQ(knot::to_json_string(member{values::join{}}), R"({"membership":"join"})");
  EXPECT_EQ(knot::to_json_string(member{std::string("ban")}), R"({"membership":"ban"})");
}

static_assert([] {
  const auto got = knot::try_read<member>(R"({"membership":"join"})");
  return got.has_value() && got->membership.index() == 0;
}());
