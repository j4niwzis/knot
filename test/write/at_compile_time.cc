import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

constexpr bool written() {
  const shapes::content one{{"$e", "m.thread"}, "a\tb", true, {{7}, {}}};
  return json(one) ==
         R"({"body":"a\tb","edited":true,"grid":[[7],[]],)"
         R"("m.relates_to":{"event_id":"$e","rel_type":"m.thread"}})";
}

constexpr bool read_back() {
  const event one{"x\x01y", -9007199254740991, {"$a", ""}};
  const auto back = knot::read<event>(json(one));
  return back && *back == one;
}

// Written, and read back, while the program is compiled.
static_assert(written());
static_assert(read_back());

TEST(Write, AtCompileTime) {
  EXPECT_TRUE(written());
  EXPECT_TRUE(read_back());
}

}  // namespace
