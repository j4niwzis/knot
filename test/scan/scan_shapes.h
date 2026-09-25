// Shared by the tests beside it; included after the imports.
#pragma once

// scan alone, on formats of the kind knot writes, written here by hand: what
// knot counts on scan doing. Nothing here imports knot.


namespace {

// Declared b then a; the JSON has a first. The scanner's parse takes the
// places in the order of the text.
struct pair_ab {
  std::string b;
  int a = 0;
};

// A leaf whose pattern is bytes said as \x escapes: ASCII, or a two-byte
// UTF-8 sequence.
struct latin {
  std::string text;
};

// An array read by a fold, a group an element.
struct numbers {
  std::vector<int> values;
};

struct outer {
  pair_ab inner;
  int n = 0;
};

template <class Type>
struct holder {
  Type value;
};

}  // namespace

template <>
struct scan::scanner<pair_ab>
    : scan::aggregate_scanner<R"(\{"a":{},"b":"{[a-z]*}"\})"> {
  static constexpr pair_ab parse(int a, std::string b) {
    return pair_ab{std::move(b), a};
  }
};

template <>
struct scan::scanner<latin> {
  static constexpr std::string_view pattern() {
    return R"("(?:[\x20\x21\x23-\x7f]|[\xc2-\xdf][\x80-\xbf])*")";
  }
  template <class Ending = scan::hands_a_failure_back>
  static constexpr std::expected<latin, scan::bad_field<>> parse(
      std::string_view text) {
    return latin{std::string(text.substr(1, text.size() - 2))};
  }
};

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
  };
  static constexpr std::string_view pattern() {
    return R"(\[(?:([0-9]+)(?:,([0-9]+))*)?\])";
  }
  static constexpr state begin_groups() { return state{}; }
  static constexpr void opened_group(state& made, std::size_t) {
    made.running = 0;
  }
  static constexpr void push_group(state& made, std::size_t, char letter) {
    made.running = made.running * 10 + (letter - '0');
  }
  static constexpr void closed_group(state& made, std::size_t) {
    made.values.push_back(made.running);
  }
  static constexpr numbers finish_groups(state made) {
    return numbers{std::move(made.values)};
  }
};

template <>
struct scan::scanner<outer>
    : scan::aggregate_scanner<R"(\{"inner":{},"n":{}\})"> {
  static constexpr outer parse(pair_ab inner, int n) {
    return outer{std::move(inner), n};
  }
};

namespace {

template <class Type>
auto read(std::string_view text) {
  return scan::scan<"{}">(text).try_of<holder<Type>>();
}

}  // namespace
