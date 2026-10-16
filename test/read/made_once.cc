// SPDX-License-Identifier: AGPL-3.0-only
// A type made once (knot::made_once): read and written through
// knot::read_once and knot::write_once, made where the type is -- here, as a
// module's interface makes them -- and read the same, at run time and at
// compile time, as a type read in place.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace once {

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
  friend consteval auto json_schema(knot::type<named>) { return knot::schema<named>().tag("m.room.name"); }
};
using content_t = knot::tagged<"type", member, named, knot::raw>;
struct unsigned_t {
  std::optional<content_t> prev_content;
  friend consteval auto json_schema(knot::type<unsigned_t>) { return knot::schema<unsigned_t>(); }
};
struct event_t {
  content_t content;
  std::string type;
  std::optional<unsigned_t> unsigned_;
  friend consteval auto json_schema(knot::type<event_t>) { return knot::schema<event_t>().member<"unsigned_">(knot::key("unsigned")); }
};
// What holds the events: read in place, the events in it through read_once.
struct page_t {
  std::vector<event_t> chunk;
  friend consteval auto json_schema(knot::type<page_t>) { return knot::schema<page_t>(); }
};

}  // namespace once

template <> inline constexpr bool knot::made_once<once::content_t> = true;
template <> inline constexpr bool knot::made_once<once::event_t> = true;
template struct knot::read_once<once::content_t>;
template struct knot::read_once<once::event_t>;
template struct knot::write_once<once::content_t>;
template struct knot::write_once<once::event_t>;

using namespace once;

constexpr std::string_view kPage =
    R"({"chunk":[{"type":"m.room.member","unsigned":{"prev_content":{"membership":"invite"}},"content":{"membership":"join","displayname":"A","x":1}},)"
    R"({"content":{"name":"Room"},"type":"m.room.name"},{"type":"m.other","content":{"y":2}}]})";

constexpr bool read_right(const page_t& page) {
  return page.chunk.size() == 3 && page.chunk[0].content.is<member>() && page.chunk[0].content.as<member>().displayname == "A" &&
         page.chunk[0].unsigned_ && page.chunk[0].unsigned_->prev_content && page.chunk[0].unsigned_->prev_content->is<member>() &&
         page.chunk[1].content.is<named>() && page.chunk[1].content.as<named>().name == "Room" && page.chunk[2].content.is<knot::raw>();
}

TEST(ReadMadeOnce, EventsInAPageAreReadThroughWhatWasMade) {
  const auto got = knot::try_read<page_t>(kPage);
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_TRUE(read_right(*got));
}

TEST(ReadMadeOnce, AndAtCompileTime) {
  static_assert([] {
    const auto got = knot::try_read<page_t>(kPage);
    return got && read_right(*got);
  }());
}

TEST(ReadMadeOnce, WrittenAsReadAContentAlone) {
  const auto got = knot::try_read<page_t>(kPage);
  ASSERT_TRUE(got);
  // The content alone, with the key its type did not have.
  EXPECT_EQ(knot::to_json_string(got->chunk[0].content), R"({"displayname":"A","membership":"join","x":1})");
  const auto again = knot::try_read<page_t>(knot::to_json_string(*got));
  ASSERT_TRUE(again) << again.error().message;
  EXPECT_TRUE(read_right(*again));
}
