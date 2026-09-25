import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, KeysSortedAndRenamed) {
  const shapes::content one{{"$e", "m.thread"}, "x", true, {{1, 2}, {}, {3}}};
  EXPECT_EQ(json(one),
            R"({"body":"x","edited":true,"grid":[[1,2],[],[3]],)"
            R"("m.relates_to":{"event_id":"$e","rel_type":"m.thread"}})");
}

}  // namespace
