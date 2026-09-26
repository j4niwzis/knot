import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

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

TEST(Read, Choices) {
  const auto got = knot::try_read<member>(
      R"({"membership": "leave", "previous": "ban", "history": ["invite", "join"],)"
      R"( "by_room": {"!a": "join"}})");
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_TRUE(std::holds_alternative<left>(got->state));
  ASSERT_TRUE(got->previous);
  ASSERT_TRUE(std::holds_alternative<std::string>(*got->previous));
  EXPECT_EQ(std::get<std::string>(*got->previous), "ban");
  ASSERT_EQ(got->history.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<invited>(got->history[0]));
  EXPECT_TRUE(std::holds_alternative<joined>(got->history[1]));
  EXPECT_TRUE(std::holds_alternative<joined>(got->by_room.at("!a")));

  // A named string in an open choice is its alternative, not the kept one.
  const auto named = knot::try_read<just<open_membership>>(R"({"it":"join"})");
  ASSERT_TRUE(named);
  EXPECT_TRUE(std::holds_alternative<joined>(named->it));

  // Escapes are decoded before the string is matched.
  EXPECT_TRUE(knot::try_read<just<std::variant<quoted>>>(R"({"it":"a\"b"})"));
  EXPECT_TRUE(knot::try_read<just<membership>>(R"({"it":"\u006aoin"})"));
}

TEST(Read, ChoicesRefused) {
  const auto unknown = knot::try_read<just<membership>>(R"({"it":  "ban"})");
  ASSERT_FALSE(unknown);
  EXPECT_EQ(unknown.error().offset, 8u);
  EXPECT_FALSE(knot::try_read<just<membership>>(R"({"it":1})"));
  EXPECT_FALSE(knot::try_read<just<membership>>(R"({"it":null})"));
  EXPECT_FALSE(knot::try_read<just<membership>>(R"({"it":"Join"})"));
  EXPECT_FALSE(knot::try_read<member>(
      R"({"membership":"knock","history":[],"by_room":{}})"));
  EXPECT_TRUE(strict<member>(R"({"by_room":{},"history":[],"membership":"join"})"));
  EXPECT_FALSE(strict<just<membership>>(R"({"it":"\u006aoin"})"));  // not canonical
}

TEST(Read, ChoicesAgainstTheTree) {
  // A string the choice does not have is content the type does not fit: kept
  // whole, as knot::value, whichever of tag and content comes first.
  for (const char* text : {R"({"content":{"membership":"invite"},"type":"m.status"})",
                           R"({"type":"m.status","content":{"membership":"invite"}})"}) {
    const auto got = knot::try_read<status_event>(text);
    ASSERT_TRUE(got) << got.error().message;
    ASSERT_TRUE(got->content.is<status>()) << text;
    EXPECT_TRUE(std::holds_alternative<invited>(got->content.as<status>().state));
  }
  const std::pair<const char*, const char*> kept[] = {
      {R"({"content":{"membership":"ban"},"type":"m.status"})",
       R"({"content":{"membership":"ban"},"type":"m.status"})"},
      {R"({"type":"m.status","content":{"membership":"ban"}})",
       R"({"content":{"membership":"ban"},"type":"m.status"})"},
      {R"({"content":{"membership":7},"type":"m.status"})",
       R"({"content":{"membership":7},"type":"m.status"})"}};
  for (const auto& [text, canonical] : kept) {
    const auto got = knot::try_read<status_event>(text);
    ASSERT_TRUE(got) << got.error().message;
    ASSERT_TRUE(got->content.is<knot::value>()) << text;
    EXPECT_EQ(knot::to_json_string(*got), canonical);
  }
  knot::value join(std::string("join"));
  knot::value ban(std::string("ban"));
  EXPECT_TRUE(knot::from_value<membership>(join));
  EXPECT_FALSE(knot::from_value<membership>(ban));
  EXPECT_TRUE(ban == knot::value(std::string("ban")));  // kept whole
  const auto open = knot::from_value<open_membership>(ban);
  ASSERT_TRUE(open);
  EXPECT_EQ(std::get<std::string>(*open), "ban");
}

}  // namespace
