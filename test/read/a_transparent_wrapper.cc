// SPDX-License-Identifier: AGPL-3.0-only
// A value given a type of its own -- a setting -- is its one member in JSON:
// read and written as what it holds, so that typing it changes nothing.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace wrapped {

struct read_receipts {
  std::optional<bool> value;
  using json_transparent = void;
};
struct display_name {
  std::string value;
  using json_transparent = void;
};
struct account {
  read_receipts receipts;
  display_name name;
  std::vector<display_name> aliases;
  friend consteval auto json_schema(knot::type<account>) {
    return knot::schema<account>().member<"receipts">(knot::key("read_receipts"));
  }
};

TEST(ATransparentWrapper, IsReadAsItsMember) {
  const auto read = knot::try_read<account>(
      std::string_view(R"({"read_receipts":false,"name":"Ann","aliases":["A","B"]})"));
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->receipts.value, std::optional<bool>(false));
  EXPECT_EQ(read->name.value, "Ann");
  ASSERT_EQ(read->aliases.size(), 2u);
  EXPECT_EQ(read->aliases[1].value, "B");
}

TEST(ATransparentWrapper, AnAbsentOptionalStaysAbsent) {
  const auto read = knot::try_read<account>(std::string_view(R"({"name":"Ann","aliases":[]})"));
  ASSERT_TRUE(read.has_value());
  EXPECT_FALSE(read->receipts.value.has_value());
}

TEST(ATransparentWrapper, IsWrittenAsItsMember) {
  const account one{{true}, {"Ann"}, {{"A"}}};
  EXPECT_EQ(knot::to_json_string(one), R"({"aliases":["A"],"name":"Ann","read_receipts":true})");
}

TEST(ATransparentWrapper, AnEmptyOptionalIsWrittenAbsent) {
  const account one{{std::nullopt}, {"Ann"}, {}};
  EXPECT_EQ(knot::to_json_string(one), R"({"aliases":[],"name":"Ann"})");
}

TEST(ATransparentWrapper, IsADocumentOfItsOwn) {
  const auto read = knot::try_read<display_name>(std::string_view(R"("Bob")"));
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->value, "Bob");
  EXPECT_EQ(knot::to_json_string(display_name{"Bob"}), R"("Bob")");
}

}  // namespace wrapped
