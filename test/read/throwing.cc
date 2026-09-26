import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

TEST(Read, Throwing) {
  const event got = knot::read<event>(R"({"content":"hi","depth":3,"prev":[]})");
  EXPECT_EQ(got.content, "hi");
  const event strict = knot::read<event>(R"({"content":"","depth":0,"prev":[]})", knot::canonical);
  EXPECT_EQ(strict.depth, 0);
  try {
    (void)knot::read<event>(R"({"content":"","depth":01,"prev":[]})");
    ADD_FAILURE() << "not thrown";
  } catch (const knot::read_failure& failure) {
    EXPECT_EQ(failure.where.offset, 23u);
    EXPECT_NE(std::string_view(failure.what()).find("at 23"), std::string_view::npos);
  }
  EXPECT_THROW((void)knot::read<event>(R"({ "content":"","depth":0,"prev":[]})", knot::canonical),
               knot::read_failure);
}

}  // namespace
