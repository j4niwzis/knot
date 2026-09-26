import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace shapes {

struct message {
  std::string msgtype;
  std::string body;
  friend bool operator==(const message&, const message&) = default;
};
consteval auto json_schema(knot::type<message>) {
  return knot::schema<message>().tag("m.room.message");
}

struct member {
  std::string membership;
  std::optional<std::string> displayname;
  friend bool operator==(const member&, const member&) = default;
};
consteval auto json_schema(knot::type<member>) {
  return knot::schema<member>().tag("m.room.member");
}

struct room_event {
  std::string type;
  knot::tagged<"type", message, member, knot::value> content;
  std::string event_id;
  friend bool operator==(const room_event&, const room_event&) = default;
};
consteval auto json_schema(knot::type<room_event>) {
  return knot::schema<room_event>();
}

// No fallback: a type nobody names is an error.
struct strict_event {
  std::string type;
  knot::tagged<"type", message, member> content;
};
consteval auto json_schema(knot::type<strict_event>) {
  return knot::schema<strict_event>();
}

}  // namespace shapes

namespace {

using shapes::message;
using shapes::member;
using shapes::room_event;

TEST(WriteBy, TheAlternativeHeld) {
  const room_event one{"m.room.member", member{"join", "x"}, "$1"};
  EXPECT_EQ(json(one),
            R"({"content":{"displayname":"x","membership":"join"},"event_id":"$1","type":"m.room.member"})");
  const auto back = knot::try_read<room_event>(json(one), knot::canonical);
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(*back, one);
}

}  // namespace
