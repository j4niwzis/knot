import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

// A knot::value is written whole as any described type is: Canonical JSON.
TEST(Write, AValueAlone) {
  knot::value::object members;
  members.emplace("b", knot::value(std::int64_t(1)));
  members.emplace("a", knot::value(knot::value::array{knot::value(true), knot::value()}));
  const knot::value one(std::move(members));
  EXPECT_EQ(knot::to_json_string(one), R"({"a":[true,null],"b":1})");
  EXPECT_EQ(json(one), knot::to_json_string(one));
  EXPECT_EQ(knot::to_json_string(knot::value(std::string("q\""))), R"("q\"")");
  EXPECT_EQ(knot::to_json_string(knot::value()), "null");
}

}  // namespace
