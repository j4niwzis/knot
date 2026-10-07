// SPDX-License-Identifier: AGPL-3.0-only
// A knot::tagged chosen by a key of an object above its own: an event's
// unsigned.prev_content, by the event's type.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace above {

struct member {
  std::string membership;
  std::optional<std::string> displayname;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<member>) {
    return knot::schema<member>().member<"rest">(knot::rest).tag("m.room.member");
  }
};
struct named {
  std::string name;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<named>) {
    return knot::schema<named>().member<"rest">(knot::rest).tag("m.room.name");
  }
};
using content_t = knot::tagged<"type", member, named, knot::raw>;
struct unsigned_t {
  std::optional<content_t> prev_content;
  std::optional<std::int64_t> age;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<unsigned_t>) { return knot::schema<unsigned_t>().member<"rest">(knot::rest); }
};
struct event_t {
  content_t content;
  std::string type;
  std::optional<unsigned_t> unsigned_;
  friend consteval auto json_schema(knot::type<event_t>) { return knot::schema<event_t>().member<"unsigned_">(knot::key("unsigned")); }
};

}  // namespace above

using namespace above;

TEST(ReadByAKeyAbove, TheTypeBeforeOrAfterUnsigned) {
  for (const std::string_view text : {
           R"({"content":{"membership":"join","displayname":"B"},"unsigned":{"prev_content":{"membership":"join","displayname":"A"},"age":5},"type":"m.room.member"})",
           R"({"type":"m.room.member","unsigned":{"age":1,"prev_content":{"membership":"join","displayname":"A"}},"content":{"membership":"join"}})",
       }) {
    const auto got = knot::try_read<event_t>(text);
    ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset << ": " << text;
    ASSERT_TRUE(got->unsigned_ && got->unsigned_->prev_content) << text;
    ASSERT_TRUE(got->unsigned_->prev_content->is<member>()) << text;
    EXPECT_EQ(got->unsigned_->prev_content->as<member>().displayname, "A");
  }
}
TEST(ReadByAKeyAbove, AnotherTypeAnotherAlternative) {
  const auto got = knot::try_read<event_t>(R"({"type":"m.room.name","content":{"name":"x"},"unsigned":{"prev_content":{"name":"y"}}})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->unsigned_->prev_content->is<named>());
  EXPECT_EQ(got->unsigned_->prev_content->as<named>().name, "y");
}
TEST(ReadByAKeyAbove, ATypeNobodyNamesStaysText) {
  const auto got = knot::try_read<event_t>(R"({"type":"x.y","content":{"a":1},"unsigned":{"prev_content":{"a":2}}})");
  ASSERT_TRUE(got) << got.error().message;
  ASSERT_TRUE(got->unsigned_->prev_content->is<knot::raw>());
  EXPECT_EQ(got->unsigned_->prev_content->as<knot::raw>().text, R"({"a":2})");
}
