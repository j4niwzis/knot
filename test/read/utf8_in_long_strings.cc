import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

std::optional<std::string> body(const std::string& text) {
  const auto got = knot::try_read<event>(R"({"content":")" + text + R"(","depth":0,"prev":[]})");
  if (!got) return std::nullopt;
  return got->content;
}

// Long enough for the blocks of 16, with what matters at every offset.
TEST(ReadUtf8, EveryCharacterAtEveryPlace) {
  const std::vector<std::string> good{"\xd0\xbf", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
                                      "\xef\xbf\xbf", "\xf4\x8f\xbf\xbf", "\xc2\x80"};
  const std::vector<std::string> bad{
      "\xc0\x80",          // overlong, two bytes
      "\xe0\x80\x80",      // overlong, three
      "\xf0\x80\x80\x80",  // overlong, four
      "\xed\xa0\x80",      // a surrogate
      "\xf4\x90\x80\x80",  // past U+10FFFF
      "\xf8\x88\x80\x80",  // no such lead
      "\x80",              // a continuation alone
      "\xd0",              // cut short
      "\xe2\x82",          // cut short
      "\xd0\xbf\xbf",      // one continuation too many
  };
  for (std::size_t at = 0; at != 40; ++at) {
    const std::string before(at, 'a');
    const std::string after(37, 'b');
    for (const std::string& one : good) {
      const std::string text = before + one + after + one;
      EXPECT_EQ(body(text), text) << at;
    }
    for (const std::string& one : bad) {
      EXPECT_FALSE(body(before + one + after)) << at << " " << one.size();
      EXPECT_FALSE(body(before + after + one)) << at << " at the end, " << one.size();
    }
  }
}

TEST(ReadUtf8, LongCyrillic) {
  std::string text;
  for (int at = 0; at != 300; ++at) text += "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 ";
  EXPECT_EQ(body(text), text);
  EXPECT_FALSE(body(text + "\xd1"));
}

}  // namespace
