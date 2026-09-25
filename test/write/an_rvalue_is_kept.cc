import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, AnRvalueIsKept) {
  auto view = knot::to_json(event{"kept", 1, {"$a"}});
  EXPECT_EQ(view | std::ranges::to<std::string>(),
            R"({"content":"kept","depth":1,"prev":["$a"]})");
}

}  // namespace
