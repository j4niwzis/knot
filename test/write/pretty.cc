import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

// Laid out for reading: a member or an element a line, two spaces a level, a
// space after each colon; empty ones kept closed; strings as they are, what
// looks like JSON inside them included.
TEST(Write, PrettyLaysOutWhatIsWritten) {
  knot::value::object inner;
  inner.emplace("note", knot::value(std::string("a, b: {c} [d] \"e\"")));
  knot::value::object members;
  members.emplace("b", knot::value(std::int64_t(1)));
  members.emplace("a", knot::value(knot::value::array{knot::value(true), knot::value()}));
  members.emplace("c", knot::value(std::move(inner)));
  members.emplace("d", knot::value(knot::value::object{}));
  members.emplace("e", knot::value(knot::value::array{}));
  const knot::value one(std::move(members));
  EXPECT_EQ(knot::to_pretty_json_string(one),
            "{\n"
            "  \"a\": [\n"
            "    true,\n"
            "    null\n"
            "  ],\n"
            "  \"b\": 1,\n"
            "  \"c\": {\n"
            "    \"note\": \"a, b: {c} [d] \\\"e\\\"\"\n"
            "  },\n"
            "  \"d\": {},\n"
            "  \"e\": []\n"
            "}");
  EXPECT_EQ(knot::to_pretty_json_string(knot::value(std::int64_t(7))), "7");
  // Read back, it is the same value.
  EXPECT_EQ(knot::try_read<knot::value>(knot::to_pretty_json_string(one)).value(), one);
}

// And at compile time, as the rest of knot.
static_assert(knot::to_pretty_json_string(knot::value(knot::value::array{knot::value(true)})) == "[\n  true\n]");

}  // namespace
