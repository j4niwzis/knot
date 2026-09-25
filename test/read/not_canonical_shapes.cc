import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, NotCanonicalShapes) {
  for (const std::string_view text : {
           R"({"content":"","depth":0,"prev":[] })",
           R"({ "content":"","depth":0,"prev":[]})",
           R"({"depth":0,"content":"","prev":[]})",
           R"({"content":"","depth":0})",
           R"({"content":"","depth":0,"prev":[],"a":1})",  // "a" sorts first
           R"({"content":"","depth":0,"prev":["a",]})",
           R"({"content":"","depth":0,"prev":["a""b"]})",
       }) {
    EXPECT_FALSE(strict<event>(text)) << text;
  }
}

}  // namespace
