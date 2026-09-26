import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(ReadOrdinary, WhiteSpaceAndAnyOrder) {
  const auto got = knot::try_read<event>(
      " { \"prev\" : [ \"$a\" ,\n \"$b\" ] ,\r\n\t\"depth\":3, \"content\" : \"hi\" } ");
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->content, "hi");
  EXPECT_EQ(got->depth, 3);
  EXPECT_EQ(got->prev, (std::vector<std::string>{"$a", "$b"}));
  // The same text is not Canonical JSON.
  EXPECT_FALSE(strict<event>(R"({ "content":"hi","depth":3,"prev":[]})"));
}

}  // namespace
