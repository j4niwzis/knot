import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace shapes {

struct ranked {
  double rank;
  std::optional<double> score;
  friend bool operator==(const ranked&, const ranked&) = default;
};
consteval auto json_schema(knot::type<ranked>) { return knot::schema<ranked>(); }

struct left_membership {
  static constexpr std::string_view json_value = "leave";
  friend constexpr bool operator==(left_membership, left_membership) = default;
};

}  // namespace shapes

namespace {

using namespace shapes;

TEST(Read, Doubles) {
  const auto got = knot::try_read<ranked>(R"({"rank": 1.5, "score": -2e3})");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->rank, 1.5);
  EXPECT_EQ(got->score, -2000.0);
  const auto whole = knot::try_read<ranked>(R"({"rank": 3})");
  ASSERT_TRUE(whole);
  EXPECT_EQ(whole->rank, 3.0);
  EXPECT_FALSE(whole->score);
  EXPECT_FALSE(knot::try_read<ranked>(R"({"rank": "1"})"));
  EXPECT_FALSE(knot::try_read<ranked>(R"({"rank": .5})"));
  // Canonical JSON has integers only.
  EXPECT_TRUE(strict<ranked>(R"({"rank":3})"));
  EXPECT_FALSE(strict<ranked>(R"({"rank":1.5})"));
  // Against the tree, and back.
  knot::value one(1.25);
  const auto from = knot::from_value<double>(one);
  ASSERT_TRUE(from);
  EXPECT_EQ(*from, 1.25);
  knot::value two(std::int64_t(2));
  EXPECT_EQ(knot::from_value<double>(two), 2.0);
  knot::value text(std::string("x"));
  EXPECT_FALSE(knot::from_value<double>(text));
  EXPECT_EQ(knot::to_json_string(*got), R"({"rank":1.5,"score":-2000})");
  // Below one, and in a map of optional members, as Matrix's room tags are.
  const auto tags = knot::try_read<std::map<std::string, ranked>>(
      R"({"m.favourite":{"rank":0.1},"u.Work":{"rank":0.7,"score":0}})");
  ASSERT_TRUE(tags) << tags.error().message << " at " << tags.error().offset;
  EXPECT_EQ(tags->at("m.favourite").rank, 0.1);
  EXPECT_EQ(tags->at("u.Work").score, 0.0);
}

TEST(Read, Documents) {
  const auto list = knot::try_read<std::vector<std::int64_t>>("[1, 2, 3]");
  ASSERT_TRUE(list);
  EXPECT_EQ(*list, (std::vector<std::int64_t>{1, 2, 3}));
  const auto map = knot::try_read<std::map<std::string, std::map<std::string, knot::value>>>(
      R"({"@a:x": {"DEV": {"k": 1}}})");
  ASSERT_TRUE(map);
  EXPECT_EQ(knot::to_json_string(*map), R"({"@a:x":{"DEV":{"k":1}}})");
  const auto any = knot::try_read<knot::value>(R"({"b": [true], "a": null})");
  ASSERT_TRUE(any);
  EXPECT_EQ(knot::to_json_string(*any), R"({"a":null,"b":[true]})");
  EXPECT_EQ(knot::try_read<std::string>(R"("hi")"), "hi");
  EXPECT_EQ(knot::try_read<double>("0.5"), 0.5);
  const auto choice = knot::try_read<std::variant<left_membership, std::string>>(R"("leave")");
  ASSERT_TRUE(choice);
  EXPECT_TRUE(std::holds_alternative<left_membership>(*choice));
  EXPECT_EQ(knot::to_json_string(*choice), R"("leave")");
  EXPECT_FALSE(knot::try_read<std::vector<std::int64_t>>("[1,"));
  EXPECT_FALSE(knot::try_read<std::vector<std::int64_t>>("[1] 2"));
  EXPECT_FALSE(strict<std::vector<std::int64_t>>("[1, 2]"));
  EXPECT_EQ(knot::to_json(*list) | std::ranges::to<std::string>(), "[1,2,3]");
}

}  // namespace
