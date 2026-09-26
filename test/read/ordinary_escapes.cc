import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(ReadOrdinary, EveryEscape) {
  const auto got = knot::try_read<event>(
      R"({"content":"\/\u0041\u00E9\ud83d\ude00\u001F\n","depth":0,"prev":[]})");
  ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset;
  EXPECT_EQ(got->content, "/A\xc3\xa9\xf0\x9f\x98\x80\x1f\n");
  for (const std::string_view body : {
           R"("\ud83d")",           // a high half alone
           R"("\ude00")",           // a low half alone
           R"("\ud83d\u0041")",    // a high half, and no low one after it
           R"("\x41")",             // no such escape
           R"("\u12")",             // too short
       }) {
    const std::string text =
        R"({"content":)" + std::string(body) + R"(,"depth":0,"prev":[]})";
    EXPECT_FALSE(knot::try_read<event>(text)) << text;
  }
}

}  // namespace
