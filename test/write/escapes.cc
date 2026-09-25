import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, OnlyTheEscapesCanonicalJsonWrites) {
  const event one{std::string("a\"b\\c/\b\f\n\r\t") + '\x01' + '\x1f' + "\x7f", -5, {}};
  EXPECT_EQ(json(one),
            std::string(R"({"content":"a\"b\\c/\b\f\n\r\t)") + "\\u0001\\u001f" +
                "\x7f" + R"(","depth":-5,"prev":[]})");
}

}  // namespace
