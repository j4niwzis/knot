import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

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

TEST(Read, Maps) {
  const auto got = knot::try_read<signed_thing>(
      R"({"signatures": {"b.org": {"ed25519:1": "sig"}, "a.org": {}}, "sender": "@x:a.org"})");
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->sender, "@x:a.org");
  ASSERT_EQ(got->signatures.size(), 2u);
  EXPECT_TRUE(got->signatures.at("a.org").empty());
  EXPECT_EQ(got->signatures.at("b.org").at("ed25519:1"), "sig");

  EXPECT_FALSE(knot::try_read<signed_thing>(
      R"({"sender":"s","signatures":{"a":{},"a":{}}})"));  // a key twice

  EXPECT_TRUE(strict<signed_thing>(
      R"({"sender":"s","signatures":{"a":{},"b":{"k":"v"}}})"));
  EXPECT_FALSE(strict<signed_thing>(
      R"({"sender":"s","signatures":{"b":{},"a":{}}})"));  // out of order
  EXPECT_FALSE(strict<signed_thing>(
      R"({"sender":"s","signatures":{"a":{"y":"1","x":"2"}}})"));
}

}  // namespace
