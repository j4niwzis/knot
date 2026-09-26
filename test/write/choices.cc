import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace shapes {

struct joined {
  static constexpr std::string_view json_value = "join";
  friend constexpr bool operator==(joined, joined) = default;
};
struct left {
  static constexpr std::string_view json_value = "leave";
  friend constexpr bool operator==(left, left) = default;
};
struct invited {
  static constexpr std::string_view json_value = "invite";
  friend constexpr bool operator==(invited, invited) = default;
};
struct quoted {
  static constexpr std::string_view json_value = "a\"b";
  friend constexpr bool operator==(quoted, quoted) = default;
};

// Closed: only these.
using membership = std::variant<joined, left, invited>;
// Open: these, or any other string, kept.
using open_membership = std::variant<joined, left, std::string>;

struct member {
  membership state;
  std::optional<open_membership> previous;
  std::vector<membership> history;
  std::map<std::string, membership> by_room;
  friend bool operator==(const member&, const member&) = default;
};
consteval auto json_schema(knot::type<member>) {
  return knot::schema<member>().member<"state">(knot::key("membership"));
}

// Only a described type is read or written whole: a choice alone, inside one.
template <class Type>
struct just {
  Type it;
  friend bool operator==(const just&, const just&) = default;
};
template <class Type>
consteval auto json_schema(knot::type<just<Type>>) {
  return knot::schema<just<Type>>();
}

struct status {
  membership state;
  friend bool operator==(const status&, const status&) = default;
};
consteval auto json_schema(knot::type<status>) {
  return knot::schema<status>().member<"state">(knot::key("membership")).tag("m.status");
}

struct status_event {
  std::string type;
  knot::tagged<"type", status, knot::value> content;
};
consteval auto json_schema(knot::type<status_event>) {
  return knot::schema<status_event>();
}

}  // namespace shapes

namespace {

using namespace shapes;

constexpr bool at_compile_time() {
  member one{left{}, open_membership(std::string("ban")), {invited{}, joined{}}, {}};
  one.by_room.emplace("!a", joined{});
  return json(one) ==
             R"({"by_room":{"!a":"join"},"history":["invite","join"],)"
             R"("membership":"leave","previous":"ban"})" &&
         knot::to_json_string(one) == json(one);
}
static_assert(at_compile_time());

TEST(Write, Choices) {
  member one{invited{}, open_membership(left{}), {}, {}};
  EXPECT_EQ(json(one), R"({"by_room":{},"history":[],"membership":"invite","previous":"leave"})");
  EXPECT_EQ(knot::to_json_string(one), json(one));
  const auto back = knot::try_read<member>(json(one), knot::canonical);
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(*back, one);

  EXPECT_EQ(json(just<std::variant<quoted>>{}), R"({"it":"a\"b"})");
  EXPECT_EQ(json(just<open_membership>{std::string("x\ny")}), R"({"it":"x\ny"})");
  EXPECT_TRUE(at_compile_time());
}

}  // namespace
