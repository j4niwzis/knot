import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace shapes {

struct message {
  std::string msgtype;
  std::string body;
};
consteval auto json_schema(knot::type<message>) {
  return knot::schema<message>().tag("m.room.message");
}

struct room_event {
  std::string type;
  knot::by<"type", message, knot::value> content;
  std::string event_id;
};
consteval auto json_schema(knot::type<room_event>) {
  return knot::schema<room_event>();
}

}  // namespace shapes

namespace {

// Read with keys the type does not have, written back with them: nothing lost,
// and in Canonical JSON's order.
TEST(WriteBy, WhatWasKeptIsWrittenBack) {
  const auto got = knot::read<shapes::room_event>(
      R"({"type":"m.room.message","content":{"msgtype":"m.text","m.relates_to":{"event_id":"$x","rel_type":"m.thread"},"body":"hi","extra":[1,{"a":null}]},"event_id":"$1"})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->content.is<shapes::message>());
  EXPECT_EQ(json(*got),
            R"({"content":{"body":"hi","extra":[1,{"a":null}],)"
            R"("m.relates_to":{"event_id":"$x","rel_type":"m.thread"},"msgtype":"m.text"},)"
            R"("event_id":"$1","type":"m.room.message"})");
  // And reads back as Canonical JSON, the same.
  const auto again = knot::read<shapes::room_event>(json(*got), knot::canonical);
  ASSERT_TRUE(again) << again.error().message;
  EXPECT_EQ(json(*again), json(*got));
}

}  // namespace
