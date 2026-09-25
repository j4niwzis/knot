import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, UnicodeAsItIs) {
  const event one{"\xd0\xbf\xe2\x82\xac\xf0\x9f\x98\x80", 0, {}};
  EXPECT_EQ(json(one),
            "{\"content\":\"\xd0\xbf\xe2\x82\xac\xf0\x9f\x98\x80\",\"depth\":0,"
            "\"prev\":[]}");
}

}  // namespace
