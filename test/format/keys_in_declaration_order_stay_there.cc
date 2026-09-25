import std;
import knot.format;
import gtest;

#include "gtest/gtest-macros.h"
#include "format_shapes.h"

namespace {

TEST(Format, KeysInDeclarationOrderStayThere) {
  EXPECT_EQ(knot::scan_format<shapes::event>.view(),
            R"(\{"content":{},"depth":{},"prev":{}\})"sv);
}

}  // namespace
