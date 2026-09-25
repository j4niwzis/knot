import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

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

TEST(Write, AnyValue) {
  knot::value::object content;
  content["z"] = knot::value::array{1, true, nullptr, "s"};
  content["a"] = knot::value::object{{"k", 2.5}};
  const any_event one{"org.example", content};
  EXPECT_EQ(json(one),
            R"({"content":{"a":{"k":2.5},"z":[1,true,null,"s"]},"type":"org.example"})");
  const auto back = knot::read<any_event>(json(one));
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(*back, one);
}

}  // namespace
