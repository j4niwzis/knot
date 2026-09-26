import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace shapes {

struct profile {
  std::optional<std::string> avatar_url;
  std::string displayname;
  std::optional<std::int64_t> age;
  friend bool operator==(const profile&, const profile&) = default;
};
consteval auto json_schema(knot::type<profile>) { return knot::schema<profile>(); }

}  // namespace shapes

namespace {

using shapes::profile;

TEST(Write, OptionalMembers) {
  EXPECT_EQ(json(profile{std::nullopt, "x", std::nullopt}), R"({"displayname":"x"})");
  EXPECT_EQ(json(profile{"u", "x", 3}),
            R"({"age":3,"avatar_url":"u","displayname":"x"})");
  EXPECT_EQ(json(profile{std::nullopt, "x", 3}), R"({"age":3,"displayname":"x"})");
  const profile one{"u", "x", std::nullopt};
  const auto back = knot::try_read<profile>(json(one), knot::canonical);
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(*back, one);
}

}  // namespace
