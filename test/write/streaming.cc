import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace streaming_shapes {
struct node {
  std::vector<node> children;
  std::string text;
};
consteval auto json_schema(knot::type<node>) { return knot::schema<node>(); }

struct open {
  std::string known;
  knot::value rest;
};
consteval auto json_schema(knot::type<open>) {
  return knot::schema<open>().member<"rest">(knot::rest);
}
}  // namespace streaming_shapes

TEST(StreamingWrite, ConcreteSinkBorrowsStringsAndEscapesAcrossPieces) {
  const event value{std::string(1000, 'x') + "\n\"\\\x01", 7, {"a", "b"}};
  struct sink {
    std::string text;
    const char* borrowed;
    bool saw_borrowed = false;
    explicit sink(const char* data) : borrowed(data) {}
    sink(const sink&) = delete;
    void operator()(std::string_view piece) {
      saw_borrowed |= piece.data() == borrowed && piece.size() == 1000;
      text += piece;
    }
  } out(value.content.data());
  knot::write_chunks(out, value);
  EXPECT_TRUE(out.saw_borrowed);
  EXPECT_EQ(out.text, knot::to_json_string(value));
}

TEST(StreamingWrite, SinkCanStopByThrowing) {
  const event value{"unused", 0, {}};
  struct stopped {};
  int calls = 0;
  EXPECT_THROW(knot::write_chunks([&](std::string_view) {
    if (++calls == 3) throw stopped{};
  }, value), stopped);
  EXPECT_EQ(calls, 3);
}

TEST(StreamingWrite, RecursiveTypesAndDeepValues) {
  streaming_shapes::node root{{}, "leaf\n"};
  for (int i = 0; i != 40; ++i) {
    std::vector<streaming_shapes::node> children;
    children.push_back(std::move(root));
    root = {std::move(children), "branch"};
  }
  const auto expected = knot::to_json_string(root);
  EXPECT_EQ(json(root), expected);
  std::string pushed;
  knot::write_chunks([&](std::string_view piece) { pushed += piece; }, root);
  EXPECT_EQ(pushed, expected);

  knot::value tree("leaf");
  for (int i = 0; i != 80; ++i) tree = knot::value::array{std::move(tree)};
  EXPECT_EQ(json(tree), knot::to_json_string(tree));
}

TEST(StreamingWrite, MovingAnOwnedViewRebindsSmallStrings) {
  auto source = knot::to_json(event{"small", 1, {"a"}});
  auto it = source.begin();
  ++it;
  auto moved = std::move(source);
  EXPECT_EQ(moved | std::ranges::to<std::string>(),
            R"({"content":"small","depth":1,"prev":["a"]})");

  auto assigned = knot::to_json(event{"discard", 9, {}});
  assigned = std::move(moved);
  EXPECT_EQ(assigned | std::ranges::to<std::string>(),
            R"({"content":"small","depth":1,"prev":["a"]})");
}

TEST(StreamingWrite, ScalarRootsAndAbsentArrayElements) {
  EXPECT_EQ(json(std::string("x\n\x01")), "\"x\\n\\u0001\"");
  EXPECT_EQ(json(42), "42");
  EXPECT_EQ(json(true), "true");
  EXPECT_EQ(json(std::numeric_limits<std::uint64_t>::max()), "18446744073709551615");
  EXPECT_EQ(json(2.5), "2.5");
  EXPECT_EQ(json(knot::raw{"[ 1 ]"}), "[ 1 ]");
  const std::vector<std::optional<int>> values{1, std::nullopt, 3};
  EXPECT_EQ(json(values), "[1,null,3]");
  EXPECT_EQ(knot::to_json_string(values), "[1,null,3]");
}

constexpr bool streamed_at_compile_time() {
  const event value{"a\n", 2, {"b"}};
  std::string text;
  knot::write_chunks([&](std::string_view piece) { text += piece; }, value);
  return text == R"({"content":"a\n","depth":2,"prev":["b"]})";
}
static_assert(streamed_at_compile_time());

TEST(StreamingWrite, OwnedMergedTreesSurviveStackGrowthAndRestart) {
  knot::value nested("tail\n");
  for (int i = 0; i != 40; ++i) nested = knot::value::array{std::move(nested)};
  streaming_shapes::open value{"small", knot::value::object{{"unknown", std::move(nested)}}};
  const std::string expected = knot::to_json_string(value);
  auto view = knot::to_json(std::move(value));
  auto pieces = view.chunks();
  auto at = pieces.begin();
  for (int i = 0; i != 30; ++i) ++at;
  // Discard a traversal after it has entered the overflow stack and merged
  // tree, then restart from the value now owned by the destination view.
  auto moved = std::move(view);
  EXPECT_EQ(moved | std::ranges::to<std::string>(), expected);
  EXPECT_EQ(moved | std::ranges::to<std::string>(), expected);
  std::string pushed;
  const auto again = knot::read<streaming_shapes::open>(expected);
  knot::write_chunks([&](std::string_view piece) { pushed += piece; }, again);
  EXPECT_EQ(pushed, expected);
}
