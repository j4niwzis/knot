import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, EagerAndLazyAgree) {
  const event one{std::string("a\"b\\c\n\x01 \xd0\xbf ") + std::string(100, 'x'), -42,
                  {"$a", "", "\t"}};
  EXPECT_EQ(knot::to_json_string(one), json(one));
  shapes::content two{{"$e", "m.thread"}, "x", true, {{1, 2}, {}, {3}}};
  EXPECT_EQ(knot::to_json_string(two), json(two));
  std::string out = "prefix:";
  knot::write(out, two);
  EXPECT_EQ(out, "prefix:" + json(two));
  EXPECT_EQ(knot::to_json_string(shapes::nothing{}), "{}");
}

}  // namespace
