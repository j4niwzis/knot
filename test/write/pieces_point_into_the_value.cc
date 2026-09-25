import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

TEST(Write, PiecesPointIntoTheValue) {
  const event one{std::string(1000, 'x'), 1, {}};
  auto view = knot::to_json(one);
  std::vector<std::string_view> pieces;
  for (const std::string_view piece : view.chunks()) pieces.push_back(piece);
  // {"content":  "  the thousand x in one piece  "  ,"depth":  1  ,"prev":  [  ]  }
  ASSERT_EQ(pieces.size(), 10u);
  EXPECT_EQ(pieces[2].size(), 1000u);
  EXPECT_EQ(pieces[2].data(), one.content.data());
}

}  // namespace
