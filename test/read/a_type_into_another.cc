import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

// One described type made another, with no text between: knot::convert.
namespace {

struct wide {
  std::string body;
  std::int64_t depth = 0;
  std::optional<std::string> format;
  std::vector<std::string> prev;
};
consteval auto json_schema(knot::type<wide>) { return knot::schema<wide>(); }

struct narrow {
  std::string body;
  std::optional<std::string> format;
};
consteval auto json_schema(knot::type<narrow>) { return knot::schema<narrow>(); }

struct needs_more {
  std::string body;
  std::string missing;
};
consteval auto json_schema(knot::type<needs_more>) { return knot::schema<needs_more>(); }

struct other_depth {
  std::string body;
  double depth = 0.0;
};
consteval auto json_schema(knot::type<other_depth>) { return knot::schema<other_depth>(); }

TEST(Convert, TakesTheMembersBothHave) {
  const auto made = knot::convert<narrow>(wide{.body = "hi", .depth = 3, .format = "html", .prev = {"a"}});
  ASSERT_TRUE(made);
  EXPECT_EQ(made->body, "hi");
  EXPECT_EQ(made->format, std::optional<std::string>("html"));
}

TEST(Convert, LeavesWhatOnlyTheSourceHas) {
  knot::value rest;
  const auto made = knot::convert<narrow>(wide{.body = "hi", .depth = 3, .prev = {"a"}}, &rest);
  ASSERT_TRUE(made);
  EXPECT_EQ(rest["depth"].as<std::int64_t>(), 3);
  EXPECT_EQ(rest["prev"].as<std::vector<knot::value>>().size(), 1u);
}

TEST(Convert, RefusesWhatDoesNotFit) {
  EXPECT_FALSE(knot::convert<needs_more>(narrow{.body = "hi"}));
}

TEST(Convert, MakesAMemberOfAnotherType) {
  const auto made = knot::convert<other_depth>(wide{.body = "hi", .depth = 3});
  ASSERT_TRUE(made);
  EXPECT_EQ(made->depth, 3.0);
}

}  // namespace
