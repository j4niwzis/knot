// knot::raw, and a knot::tagged with it among its alternatives: no tree made.
// The content is kept as its text until the tag is known, then read into the
// alternative the tag names -- whichever order the tag and the content come
// in; what no tag names, or what does not fit the named one, stays the text,
// byte for byte, and is written back as it came.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace raw_shapes {

struct message {
  std::string msgtype;
  std::string body;
  friend bool operator==(const message&, const message&) = default;
};
consteval auto json_schema(knot::type<message>) {
  return knot::schema<message>().tag("m.room.message");
}

struct member {
  std::string membership;
  std::optional<std::string> displayname;
  friend bool operator==(const member&, const member&) = default;
};
consteval auto json_schema(knot::type<member>) {
  return knot::schema<member>().tag("m.room.member");
}

struct event {
  std::string type;
  knot::tagged<"type", message, member, knot::raw> content;
  std::string event_id;
};
consteval auto json_schema(knot::type<event>) { return knot::schema<event>(); }

struct holder {
  knot::raw anything;
  int after = 0;
};
consteval auto json_schema(knot::type<holder>) { return knot::schema<holder>(); }

struct open_one {
  std::string known;
  knot::raw rest;
};
consteval auto json_schema(knot::type<open_one>) {
  return knot::schema<open_one>().member<"rest">(knot::rest);
}

}  // namespace raw_shapes

using namespace raw_shapes;

TEST(RawText, IsKeptByteForByte) {
  const std::string text = R"({"anything": {"b" :[1, 2.50,"xA"], "a":null} ,"after":7})";
  const holder got = knot::read<holder>(text);
  EXPECT_EQ(got.anything.text, R"({"b" :[1, 2.50,"xA"], "a":null})");
  EXPECT_EQ(got.after, 7);
}

TEST(RawText, IsWrittenAsItCame) {
  holder one{knot::raw{R"([1, {"z":2}])"}, 3};
  EXPECT_EQ(knot::to_json_string(one), R"({"after":3,"anything":[1, {"z":2}]})");
}

TEST(RawText, AloneAtTheTop) {
  EXPECT_EQ(knot::read<knot::raw>(R"(  "just a string" )").text, R"("just a string")");
}

TEST(TaggedWithRaw, TagFirst) {
  const event got = knot::read<event>(
      R"({"type":"m.room.message","content":{"msgtype":"m.text","body":"hi"},"event_id":"$1"})");
  ASSERT_TRUE(got.content.is<message>());
  EXPECT_EQ(got.content.as<message>(), (message{"m.text", "hi"}));
}

TEST(TaggedWithRaw, TagAfterTheContent) {
  const event got = knot::read<event>(
      R"({"content":{"membership":"join","displayname":"A"},"event_id":"$1","type":"m.room.member"})");
  ASSERT_TRUE(got.content.is<member>());
  EXPECT_EQ(got.content.as<member>(), (member{"join", "A"}));
}

TEST(TaggedWithRaw, UnnamedStaysText) {
  const event got = knot::read<event>(
      R"({"content":{"key":"👍","m.relates_to":{"rel_type":"m.annotation"}},"type":"m.reaction","event_id":"$2"})");
  ASSERT_TRUE(got.content.is<knot::raw>());
  EXPECT_EQ(got.content.as<knot::raw>().text, R"({"key":"👍","m.relates_to":{"rel_type":"m.annotation"}})");
}

TEST(TaggedWithRaw, WhatDoesNotFitStaysText) {
  // A message without its body: named, but it does not fit.
  const event got = knot::read<event>(R"({"type":"m.room.message","content":{"msgtype":7},"event_id":"$3"})");
  ASSERT_TRUE(got.content.is<knot::raw>());
  EXPECT_EQ(got.content.as<knot::raw>().text, R"({"msgtype":7})");
}

TEST(TaggedWithRaw, NoTagStaysText) {
  const event got = knot::read<event>(R"({"content":{"x":1},"event_id":"$4","type":""})");
  ASSERT_TRUE(got.content.is<knot::raw>());
}

TEST(TaggedWithRaw, RoundTrip) {
  const std::string text = R"({"content":{"a":[1,2]},"event_id":"$5","type":"org.example"})";
  EXPECT_EQ(knot::to_json_string(knot::read<event>(text)), text);
}

// knot::raw as a rest member: the keys the type has no member for, kept as
// the text of one object -- each value's text as it came, never read.
TEST(RawRest, KeepsTheOtherKeysAsText) {
  const open_one got = knot::read<open_one>(R"({"x":[1, 2], "known":"k","y\"q": {"a":null}})");
  EXPECT_EQ(got.known, "k");
  EXPECT_EQ(got.rest.text, R"({"x":[1, 2],"y\"q":{"a":null}})");
}

TEST(RawRest, NoOtherKeysKeepNothing) {
  const open_one got = knot::read<open_one>(R"({"known":"k"})");
  EXPECT_TRUE(got.rest.text.empty());
  EXPECT_EQ(knot::to_json_string(got), R"({"known":"k"})");
}

TEST(RawRest, IsWrittenBackAmongTheMembers) {
  const open_one got = knot::read<open_one>(R"({"x":[1, 2], "known":"k","y\"q": {"a":null}})");
  EXPECT_EQ(knot::to_json_string(got), R"({"known":"k","x":[1,2],"y\"q":{"a":null}})");
}

namespace {
template <class Type>
auto from_stream(const std::string& text) {
  std::istringstream input(text);
  input >> std::noskipws;
  return knot::try_read<Type>(std::ranges::istream_view<char>(input));
}
}  // namespace

TEST(RawStream, CapturesOnlyTheValueAndReadsTheFollowingMember) {
  const auto got = from_stream<holder>(
      R"({"anything": {"b" : [1, 2.50,"x\u0041","👍"]}, "after":7})");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->anything.text, R"({"b" : [1, 2.50,"x\u0041","👍"]})");
  EXPECT_EQ(got->after, 7);
  const auto scalar = from_stream<knot::raw>(" \"a b\" \n");
  ASSERT_TRUE(scalar.has_value());
  EXPECT_EQ(scalar->text, R"("a b")");
}

TEST(RawStream, RawRestKeepsValuesAndEscapedKeys) {
  const auto got = from_stream<open_one>(
      R"({"x":[1, 2], "known":"k","y\"q": {"a":null}})");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->known, "k");
  EXPECT_EQ(got->rest.text, R"({"x":[1, 2],"y\"q":{"a":null}})");
}

TEST(RawStream, TaggedContentWorksInEitherOrderAndKeepsFallbacks) {
  for (const auto text : {
      R"({"type":"m.room.message","content":{"body":"hi","msgtype":"m.text"},"event_id":"$1"})",
      R"({"content":{"body":"hi","msgtype":"m.text"},"event_id":"$1","type":"m.room.message"})"}) {
    const auto got = from_stream<event>(text);
    ASSERT_TRUE(got.has_value());
    ASSERT_TRUE(got->content.is<message>());
    EXPECT_EQ(got->content.as<message>(), (message{"m.text", "hi"}));
  }
  for (const auto tag : {"m.room.message", "org.unknown"}) {
    const auto got = from_stream<event>(
        std::string(R"({"content": {"msgtype" : 7}, "type":")") + tag +
        R"(","event_id":"$1"})");
    ASSERT_TRUE(got.has_value());
    ASSERT_TRUE(got->content.is<knot::raw>());
    EXPECT_EQ(got->content.as<knot::raw>().text, R"({"msgtype" : 7})");
  }
}

TEST(RawStream, JoinsChunksAcrossEscapesNumbersAndUtf8) {
  const std::string text = R"({"a":"\uD83D\uDC4D👍","b":-2.50e+3})";
  for (std::size_t split = 0; split <= text.size(); ++split) {
    const std::array<std::string_view, 2> pieces{
        std::string_view(text).substr(0, split), std::string_view(text).substr(split)};
    const auto got = knot::try_read<knot::raw>(pieces | std::views::join);
    ASSERT_TRUE(got.has_value()) << split;
    EXPECT_EQ(got->text, text);
  }
}

TEST(RawStream, ValidationAndOffsetsAgreeWithMemoryInput) {
  const std::vector<std::string> texts{
      R"({"a":[1,]})", R"({"a":"\uD800"})", "[1e+]", "[01]", "true false",
      std::string("[\"\xff\"]"), std::string(129, '[') + "0" + std::string(129, ']'),
      R"({"b":1,"a":2})", R"({"a":1,"a":2})", R"({"a":"\u0041"})", "[1, 2]",
      R"({"a":[1,2],"b":"👍"})"};
  for (const auto& text : texts) {
    for (bool canonical : {false, true}) {
      std::istringstream input(text);
      input >> std::noskipws;
      auto chars = std::ranges::istream_view<char>(input);
      const auto streamed = canonical ? knot::try_read<knot::raw>(chars, knot::canonical)
                                      : knot::try_read<knot::raw>(chars);
      const auto memory = canonical ? knot::try_read<knot::raw>(text, knot::canonical)
                                    : knot::try_read<knot::raw>(text);
      ASSERT_EQ(streamed.has_value(), memory.has_value()) << text;
      if (memory) EXPECT_EQ(streamed->text, memory->text);
      else {
        EXPECT_EQ(streamed.error().offset, memory.error().offset) << text;
        EXPECT_EQ(streamed.error().message, memory.error().message) << text;
      }
    }
  }
}

constexpr bool raw_from_a_view_at_compile_time() {
  constexpr std::string_view text = R"({"a":"\u0041"})";
  auto chars = text | std::views::transform([](char c) { return c; });
  auto got = knot::try_read<knot::raw>(chars);
  return got && got->text == text;
}
static_assert(raw_from_a_view_at_compile_time());
