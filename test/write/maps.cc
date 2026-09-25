import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace shapes {

// The shape of a signed thing in Matrix: signatures by server, then by key.
struct signed_thing {
  std::string sender;
  std::map<std::string, std::map<std::string, std::string>> signatures;
  friend bool operator==(const signed_thing&, const signed_thing&) = default;
};
consteval auto json_schema(knot::type<signed_thing>) {
  return knot::schema<signed_thing>();
}

}  // namespace shapes

namespace {

using shapes::signed_thing;

TEST(Write, Maps) {
  signed_thing one;
  one.sender = "@x:a.org";
  one.signatures["b.org"]["ed25519:2"] = "two";
  one.signatures["b.org"]["ed25519:1"] = "one";
  one.signatures["a.org"];
  EXPECT_EQ(json(one),
            R"({"sender":"@x:a.org","signatures":{"a.org":{},)"
            R"("b.org":{"ed25519:1":"one","ed25519:2":"two"}}})");
  const auto back = knot::read<signed_thing>(json(one), knot::canonical);
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(*back, one);
  EXPECT_EQ(json(signed_thing{"s", {}}), R"({"sender":"s","signatures":{}})");
}

}  // namespace
