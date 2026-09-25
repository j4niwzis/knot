import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, AnEvent) {
  const event one{"hi", 3, {"$a", "$b"}};
  EXPECT_EQ(json(one), R"({"content":"hi","depth":3,"prev":["$a","$b"]})");
}

}  // namespace
