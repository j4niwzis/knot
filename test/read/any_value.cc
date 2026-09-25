import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace shapes {

// An event whose content nobody here describes.
struct any_event {
  std::string type;
  knot::value content;
  friend bool operator==(const any_event&, const any_event&) = default;
};
consteval auto json_schema(knot::type<any_event>) {
  return knot::schema<any_event>();
}

}  // namespace shapes

namespace {

using shapes::any_event;

TEST(Read, AnyValue) {
  const auto got = knot::read<any_event>(
      R"({"type":"org.example","content":{"body":"hi","n":[1,2.5,-3e2,true,null,{"x":{}}],"big":1e20}})");
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  const knot::value& content = got->content;
  EXPECT_EQ(content["body"].as<std::string>(), "hi");
  const auto& n = content["n"].as<knot::value::array>();
  ASSERT_EQ(n.size(), 6u);
  EXPECT_EQ(n[0].as<std::int64_t>(), 1);
  EXPECT_EQ(n[1].as<double>(), 2.5);
  EXPECT_EQ(n[2].as<std::int64_t>(), -300);  // whole, so an integer
  EXPECT_TRUE(n[3].as<bool>());
  EXPECT_TRUE(n[4].is_null());
  EXPECT_TRUE(n[5]["x"].is<knot::value::object>());
  EXPECT_EQ(content["big"].as<double>(), 1e20);
  EXPECT_TRUE(content["absent"].is_null());

  EXPECT_FALSE(knot::read<any_event>(R"({"type":"t","content":1e400})"));
  EXPECT_FALSE(knot::read<any_event>(R"({"type":"t","content":{"a":1,"a":2}})"));
  const std::string deep = std::string(200, '[') + std::string(200, ']');
  EXPECT_FALSE(knot::read<any_event>(R"({"type":"t","content":)" + deep + "}"));

  // Canonical: integers only, keys sorted all the way down.
  EXPECT_TRUE(strict<any_event>(R"({"content":{"a":[1,{"b":null}],"c":"d"},"type":"t"})"));
  EXPECT_FALSE(strict<any_event>(R"({"content":2.5,"type":"t"})"));
  EXPECT_FALSE(strict<any_event>(R"({"content":{"c":1,"a":2},"type":"t"})"));
}

}  // namespace
