import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace shapes {

// An event that keeps what it does not describe.
struct kept_event {
  std::string type;
  std::string event_id;
  std::optional<std::int64_t> origin_server_ts;
  knot::value extra;
};
consteval auto json_schema(knot::type<kept_event>) {
  return knot::schema<kept_event>().member<"extra">(knot::rest);
}

}  // namespace shapes

namespace {

using shapes::kept_event;

TEST(ReadRest, KeysTheTypeDoesNotHaveAreKept) {
  const std::string text =
      R"({"type":"m.room.name","state_key":"","unsigned":{"age":5,"x":[1,null]},)"
      R"("event_id":"$e","z":true,"origin_server_ts":7})";
  const auto got = knot::read<kept_event>(text);
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->type, "m.room.name");
  EXPECT_EQ(got->origin_server_ts, 7);
  EXPECT_EQ(got->extra["state_key"].as<std::string>(), "");
  EXPECT_EQ(got->extra["unsigned"]["age"].as<std::int64_t>(), 5);
  EXPECT_TRUE(got->extra["z"].as<bool>());
  EXPECT_TRUE(got->extra["type"].is_null());

  // Written back whole, in Canonical JSON's order, and read back strictly.
  const std::string canonical = knot::to_json_string(*got);
  EXPECT_EQ(canonical,
            R"({"event_id":"$e","origin_server_ts":7,"state_key":"","type":"m.room.name",)"
            R"("unsigned":{"age":5,"x":[1,null]},"z":true})");
  EXPECT_EQ(knot::to_json(*got) | std::ranges::to<std::string>(), canonical);
  const auto again = knot::read<kept_event>(canonical, knot::canonical);
  ASSERT_TRUE(again) << again.error().message << " at " << again.error().offset;
  EXPECT_EQ(knot::to_json_string(*again), canonical);

  // Nothing kept: nothing written for it.
  const auto plain = knot::read<kept_event>(R"({"type":"t","event_id":"$e"})");
  ASSERT_TRUE(plain);
  EXPECT_EQ(knot::to_json_string(*plain), R"({"event_id":"$e","type":"t"})");
  // A key twice is refused, the kept ones too.
  EXPECT_FALSE(knot::read<kept_event>(R"({"type":"t","event_id":"$e","z":1,"z":2})"));
}

}  // namespace
