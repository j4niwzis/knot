import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace shapes {

struct counts {
  std::flat_map<std::string, std::int64_t> by_name;
  friend bool operator==(const counts&, const counts&) = default;
};
consteval auto json_schema(knot::type<counts>) { return knot::schema<counts>(); }

}  // namespace shapes

namespace {

using shapes::counts;

TEST(Write, FlatMaps) {
  counts one;
  one.by_name["b"] = 2;
  one.by_name["a"] = 1;
  EXPECT_EQ(json(one), R"({"by_name":{"a":1,"b":2}})");
  const auto back = knot::read<counts>(json(one), knot::canonical);
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(*back, one);
}

}  // namespace
