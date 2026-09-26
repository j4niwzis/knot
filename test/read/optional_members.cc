import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

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

TEST(Read, OptionalMembers) {
  const auto some = knot::try_read<profile>(R"({"displayname":"x"})");
  ASSERT_TRUE(some) << some.error().message;
  EXPECT_EQ(*some, (profile{std::nullopt, "x", std::nullopt}));

  const auto all = knot::try_read<profile>(
      R"({"avatar_url": null, "displayname":"x", "age":3})");
  ASSERT_TRUE(all) << all.error().message;
  EXPECT_EQ(*all, (profile{std::nullopt, "x", 3}));

  EXPECT_FALSE(knot::try_read<profile>(R"({"age":3})"));  // displayname is not optional

  // Canonical: absent keys stepped over, and still in order.
  EXPECT_TRUE(strict<profile>(R"({"displayname":"x"})"));
  const auto age = strict<profile>(R"({"age":3,"displayname":"x"})");
  ASSERT_TRUE(age) << age.error().message;
  EXPECT_EQ(age->age, 3);
  EXPECT_FALSE(age->avatar_url);
  EXPECT_TRUE(strict<profile>(R"({"age":3,"b":1,"displayname":"x"})"));
  EXPECT_TRUE(strict<profile>(R"({"b":1,"displayname":"x"})"));
  EXPECT_FALSE(strict<profile>(R"({"avatar_url":"u","age":3,"displayname":"x"})"));
  EXPECT_FALSE(strict<profile>(R"({"age":3})"));
}

}  // namespace
