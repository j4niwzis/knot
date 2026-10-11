import std;
import splice;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using call_version = spl::variant<std::string, std::int64_t>;

struct content {
  call_version version;
};
consteval auto json_schema(knot::type<content>) {
  return knot::schema<content>().tag("call");
}
struct event {
  knot::tagged<"type", content, knot::value> content;
  std::string type;
};
consteval auto json_schema(knot::type<event>) { return knot::schema<event>(); }

TEST(StringInteger, ReadsBothWireForms) {
  const auto legacy = knot::try_read<call_version>("0");
  ASSERT_TRUE(legacy);
  EXPECT_EQ(spl::get<std::int64_t>(*legacy), 0);
  const auto modern = knot::try_read<call_version>(R"("1")");
  ASSERT_TRUE(modern);
  EXPECT_EQ(spl::get<std::string>(*modern), "1");
  const auto escaped = knot::try_read<call_version>(R"("\u0031")");
  ASSERT_TRUE(escaped);
  EXPECT_EQ(spl::get<std::string>(*escaped), "1");
}

TEST(StringInteger, RefusesOtherShapesAndOverflow) {
  for (std::string_view text : {"null", "true", "[]", "{}", "0.5",
                                "9223372036854775808", "\"unterminated"}) {
    EXPECT_FALSE(knot::try_read<call_version>(text)) << text;
  }
  const auto whole = knot::try_read<call_version>("1e0");
  ASSERT_TRUE(whole);
  EXPECT_EQ(spl::get<std::int64_t>(*whole), 1);
}

TEST(StringInteger, SupportsBothVariantsAndAlternativeOrders) {
  const auto reverse = knot::try_read<spl::variant<std::int64_t, std::string>>(R"("1")");
  ASSERT_TRUE(reverse);
  EXPECT_EQ(spl::get<std::string>(*reverse), "1");
  const auto standard = knot::try_read<std::variant<std::string, std::int64_t>>("0");
  ASSERT_TRUE(standard);
  EXPECT_EQ(std::get<std::int64_t>(*standard), 0);
  EXPECT_FALSE((knot::try_read<std::variant<std::uint16_t, std::string>>("65536")));
}

TEST(StringInteger, TaggedContentKeepsTypedVersionsAndUnknownValues) {
  for (std::string_view text : {R"({"type":"call","content":{"version":0}})",
                                R"({"content":{"version":"1"},"type":"call"})"}) {
    auto made = knot::try_read<event>(text);
    ASSERT_TRUE(made);
    EXPECT_EQ(made->content.data().index(), 0);
    const auto written = knot::to_json_string(*made);
    auto back = knot::try_read<event>(written);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->content.data().index(), 0);
    EXPECT_EQ(knot::to_json_string(*back), written);
  }
  auto unknown = knot::try_read<event>(R"({"type":"call","content":{"version":0.5}})");
  ASSERT_TRUE(unknown);
  EXPECT_EQ(unknown->content.data().index(), 1);
  EXPECT_EQ(knot::to_json_string(*unknown),
            R"({"content":{"version":0.5},"type":"call"})");
}

TEST(StringInteger, EagerAndLazyWritersPreserveWireTypes) {
  for (const call_version& one : {call_version(std::int64_t{0}), call_version(std::string("1")),
                            call_version(std::string("quote\"\n"))}) {
    const auto eager = knot::to_json_string(one);
    const auto lazy = std::ranges::to<std::string>(knot::to_json(one));
    EXPECT_EQ(eager, lazy);
    auto back = knot::try_read<call_version>(eager);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->index(), one.index());
    EXPECT_EQ(knot::to_json_string(*back), eager);
  }
  EXPECT_EQ(knot::to_json_string(call_version(std::int64_t{0})), "0");
  EXPECT_EQ(knot::to_json_string(call_version(std::string("1"))), R"("1")");
  EXPECT_EQ(knot::to_json_string(std::variant<std::int64_t, std::string>(std::string("1"))),
            R"("1")");
}

}  // namespace
