import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, RenamedKeysAndNestedObjects) {
  const auto got = knot::read<shapes::content>(
      R"({"body":"x","edited":true,)"
      R"("m.relates_to":{"event_id":"$e","rel_type":"m.thread"}})");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->body, "x");
  EXPECT_TRUE(got->edited);
  EXPECT_EQ(got->relates_to.event_id, "$e");
  EXPECT_EQ(got->relates_to.rel_type, "m.thread");
}

}  // namespace
