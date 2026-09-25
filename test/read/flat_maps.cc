import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace shapes {

struct counts {
  std::flat_map<std::string, std::int64_t> by_name;
  friend bool operator==(const counts&, const counts&) = default;
};
consteval auto json_schema(knot::type<counts>) { return knot::schema<counts>(); }

}  // namespace shapes

namespace {

using shapes::counts;

TEST(Read, FlatMaps) {
  const auto got = knot::read<counts>(R"({"by_name": {"b": 2, "a": 1, "c": 3}})");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->by_name.keys(), (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(got->by_name.values(), (std::vector<std::int64_t>{1, 2, 3}));
  EXPECT_FALSE(knot::read<counts>(R"({"by_name":{"b":1,"a":0,"b":2}})"));

  const auto sorted = strict<counts>(R"({"by_name":{"a":1,"b":2}})");
  ASSERT_TRUE(sorted) << sorted.error().message;
  EXPECT_EQ(sorted->by_name.at("b"), 2);
  EXPECT_FALSE(strict<counts>(R"({"by_name":{"b":2,"a":1}})"));
}

}  // namespace
