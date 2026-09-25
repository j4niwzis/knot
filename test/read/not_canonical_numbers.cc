import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, NotCanonicalNumbers) {
  for (const std::string_view depth :
       {"01", "-0", "1.0", "1e3", "+1", "9007199254740992",
        "-9007199254740992", "12345678901234567"}) {
    const std::string text =
        R"({"content":"","depth":)" + std::string(depth) + R"(,"prev":[]})";
    EXPECT_FALSE(strict<event>(text)) << text;
  }
  EXPECT_TRUE(strict<event>(
      R"({"content":"","depth":9007199254740991,"prev":[]})"));
  EXPECT_TRUE(strict<event>(
      R"({"content":"","depth":-9007199254740991,"prev":[]})"));
}

}  // namespace
