import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, NotCanonicalStrings) {
  for (const std::string_view body : {
           R"("\/")",        // an escape Canonical JSON does not write
           R"("\u0041")",    // A, which is written as itself
           R"("\u001F")",    // upper case
           R"("\u000a")",    // \n has a short form
           "\"\x01\"",        // a control unescaped
           "\"\xff\"",        // not UTF-8
           "\"\xc0\x80\"",    // overlong
           "\"\xed\xa0\x80\"",  // a surrogate
           "\"\xf4\x90\x80\x80\"",  // past U+10FFFF
       }) {
    const std::string text =
        R"({"content":)" + std::string(body) + R"(,"depth":0,"prev":[]})";
    EXPECT_FALSE(knot::read<event>(text)) << text;
  }
}

}  // namespace
