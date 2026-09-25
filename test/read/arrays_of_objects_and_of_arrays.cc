import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, ArraysOfObjectsAndOfArrays) {
  const auto got = strict<shapes::many>(
      R"({"grid":[[1,2],[],[3]],)"
      R"("links":[{"event_id":"$a","rel_type":"r"},{"event_id":"$b","rel_type":"s"}]})");
  ASSERT_TRUE(got) << got.error().message;
  EXPECT_EQ(got->grid,
            (std::vector<std::vector<std::int64_t>>{{1, 2}, {}, {3}}));
  ASSERT_EQ(got->links.size(), 2u);
  EXPECT_EQ(got->links[1].event_id, "$b");
}

}  // namespace
