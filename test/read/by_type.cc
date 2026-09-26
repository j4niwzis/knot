import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

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

// A fuller message: what message has, and a relation.
struct rich_message {
  std::string msgtype;
  std::string body;
  std::optional<relates> relates_to;
  friend bool operator==(const rich_message&, const rich_message&) = default;
};
consteval auto json_schema(knot::type<rich_message>) {
  return knot::schema<rich_message>()
      .member<"relates_to">(knot::key("m.relates_to"))
      .tag("org.example.rich");
}

struct rich_event {
  std::string type;
  knot::tagged<"type", message, rich_message, knot::value> content;
};
consteval auto json_schema(knot::type<rich_event>) {
  return knot::schema<rich_event>();
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

TEST(ReadBy, TheTagBeforeOrAfter) {
  for (const std::string_view text : {
           R"({"type":"m.room.message","content":{"msgtype":"m.text","body":"hi"},"event_id":"$1"})",
           R"({"content":{"body":"hi","msgtype":"m.text"},"event_id":"$1","type":"m.room.message"})",
       }) {
    const auto got = knot::read<room_event>(text);
    ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset << ": " << text;
    ASSERT_TRUE(got->content.is<message>()) << text;
    EXPECT_EQ(got->content.as<message>().body, "hi");
  }
  // Read into message first, then handed over to member when the tag came.
  const auto handed = knot::read<room_event>(
      R"({"content":{"membership":"join","displayname":"x"},"event_id":"$2","type":"m.room.member"})");
  ASSERT_TRUE(handed) << handed.error().message;
  ASSERT_TRUE(handed->content.is<member>());
  EXPECT_EQ(handed->content.as<member>().membership, "join");
  EXPECT_EQ(handed->content.as<member>().displayname, "x");
}

TEST(ReadBy, ContentThatFitsButIsNotNamed) {
  // A perfect message under a tag nobody names: it is a value.
  const auto custom = knot::read<room_event>(
      R"({"content":{"body":"hi","msgtype":"m.text"},"event_id":"$3","type":"org.example.note"})");
  ASSERT_TRUE(custom) << custom.error().message;
  ASSERT_TRUE(custom->content.is<knot::value>());
  EXPECT_EQ(custom->content.as<knot::value>()["body"].as<std::string>(), "hi");

  const auto first = knot::read<room_event>(
      R"({"type":"org.example.note","content":{"body":"hi","msgtype":"m.text"},"event_id":"$3"})");
  ASSERT_TRUE(first) << first.error().message;
  EXPECT_TRUE(first->content.is<knot::value>());
}

TEST(ReadBy, ANamedTagWhoseContentDoesNotFit) {
  // Redacted: the tag says message, the content is not one.
  for (const std::string_view text : {
           R"({"type":"m.room.message","content":{},"event_id":"$4"})",
           R"({"content":{},"event_id":"$4","type":"m.room.message"})",
           R"({"type":"m.room.message","content":{"body":"hi","extra":[1,{"a":null}]},"event_id":"$4"})",
           R"({"content":{"body":5,"msgtype":"m.text"},"event_id":"$4","type":"m.room.message"})",
       }) {
    const auto got = knot::read<room_event>(text);
    ASSERT_TRUE(got) << got.error().message << ": " << text;
    EXPECT_TRUE(got->content.is<knot::value>()) << text;
  }
  // What was read before the turn is in the tree too.
  const auto kept = knot::read<room_event>(
      R"({"type":"m.room.message","content":{"body":"hi","extra":[1,{"a":null}]},"event_id":"$4"})");
  ASSERT_TRUE(kept);
  const knot::value& tree = kept->content.as<knot::value>();
  EXPECT_EQ(tree["body"].as<std::string>(), "hi");
  EXPECT_TRUE(tree["extra"].is<knot::value::array>());
  EXPECT_TRUE(tree["extra"].as<knot::value::array>()[1]["a"].is_null());
}

TEST(ReadBy, TurnedButStillTheNamedType) {
  // A key message does not have turns the reading into a tree; the tag then
  // makes a message of it again, as reading a message passes such keys over.
  const auto extra = knot::read<room_event>(
      R"({"type":"m.room.message","content":{"body":"hi","msgtype":"m.text","extra":[1,{"a":null}]},"event_id":"$4"})");
  ASSERT_TRUE(extra) << extra.error().message;
  ASSERT_TRUE(extra->content.is<message>());
  EXPECT_EQ(extra->content.as<message>().body, "hi");
}

TEST(ReadBy, NullForAnOptional) {
  // null turns the reading into a tree, which keeps it; the tag then makes a
  // member of it, whose optional is empty.
  const auto got = knot::read<room_event>(
      R"({"type":"m.room.member","content":{"membership":"join","displayname":null},"event_id":"$5"})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->content.is<member>());
  EXPECT_EQ(got->content.as<member>().membership, "join");
  EXPECT_FALSE(got->content.as<member>().displayname);
}

TEST(ReadBy, Canonical) {
  const auto got = knot::read<room_event>(
      R"({"content":{"body":"hi","msgtype":"m.text"},"event_id":"$1","type":"m.room.message"})",
      knot::canonical);
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_TRUE(got->content.is<message>());
  EXPECT_FALSE(knot::read<room_event>(
      R"({"content":{"msgtype":"m.text","body":"hi"},"event_id":"$1","type":"m.room.message"})",
      knot::canonical));
}

TEST(ReadBy, NoFallback) {
  EXPECT_TRUE(knot::read<shapes::strict_event>(
      R"({"type":"m.room.member","content":{"membership":"leave"}})"));
  EXPECT_FALSE(knot::read<shapes::strict_event>(
      R"({"type":"org.example","content":{"membership":"leave"}})"));
  EXPECT_FALSE(knot::read<shapes::strict_event>(
      R"({"type":"m.room.member","content":{}})"));
}

TEST(ReadBy, KeptKeys) {
  // Extra keys: still a message, the extras in unknown().
  const auto got = knot::read<room_event>(
      R"({"content":{"body":"hi","msgtype":"m.text","m.relates_to":{"rel_type":"m.thread","event_id":"$x"},"extra":[1]},"event_id":"$1","type":"m.room.message"})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->content.is<message>());
  EXPECT_EQ(got->content.unknown["m.relates_to"]["rel_type"].as<std::string>(), "m.thread");
  EXPECT_TRUE(got->content.unknown["extra"].is<knot::value::array>());
  EXPECT_TRUE(got->content.unknown["body"].is_null());
}

TEST(ReadBy, AFullerTypeTakesTheKeptKeys) {
  // Read as message (its first key is "body"); the tag then names the fuller
  // type, which has m.relates_to: the kept value goes into it.
  const auto got = knot::read<shapes::rich_event>(
      R"({"content":{"body":"hi","m.relates_to":{"event_id":"$x","rel_type":"m.thread"},"msgtype":"m.text","other":true},"type":"org.example.rich"})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->content.is<shapes::rich_message>());
  const auto& rich = got->content.as<shapes::rich_message>();
  EXPECT_EQ(rich.body, "hi");
  ASSERT_TRUE(rich.relates_to);
  EXPECT_EQ(rich.relates_to->event_id, "$x");
  EXPECT_TRUE(got->content.unknown["other"].as<bool>());
  EXPECT_TRUE(got->content.unknown["m.relates_to"].is_null());
}

TEST(ReadBy, KeptKeysGoBackIntoTheTree) {
  // A tag nobody names: the tree is the typed value and the kept keys, whole.
  const auto got = knot::read<room_event>(
      R"({"content":{"body":"hi","msgtype":"m.text","z":{"deep":[1,2]}},"event_id":"$1","type":"org.example"})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->content.is<knot::value>());
  const knot::value& tree = got->content.as<knot::value>();
  EXPECT_EQ(tree["body"].as<std::string>(), "hi");
  EXPECT_EQ(tree["z"]["deep"].as<knot::value::array>().size(), 2u);
}

}  // namespace
