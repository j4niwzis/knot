// knot.read: a described type read out of JSON, ordinary or canonical.
//
//   knot::read<event>(text)                   // any JSON (RFC 8259)
//   knot::read<event>(text, knot::canonical)  // Canonical JSON and nothing else
//
// Both give std::expected<event, knot::error>, and both come from the type:
// no tree is built and no automaton either. For a type known in advance JSON is
// decided by the next character everywhere, so the reader is written straight
// from it -- a function a type, the nesting the type's own -- and reads any
// input range once, a character at a time, looking one ahead.
//
// Ordinary JSON may have white space anywhere, keys in any order, any escape
// (\u0041, upper-case hex, surrogate pairs), and a number in any spelling -- as
// long as it is a whole number in the range of its member. Canonical JSON has
// none of that: no white space, keys sorted by their bytes, only the escapes it
// must have, integers as digits. In both, a key the type does not have is
// passed over, a key twice is refused, and text that is not UTF-8 is refused.
export module knot.read;

import std;
import boost.pfr;
export import knot.format;
export import knot.value;

export namespace knot {

// What went wrong, and how many characters in.
struct error {
  std::string_view message;
  std::size_t offset = 0;
};

// Asks for Canonical JSON and nothing else.
struct canonical_t {
  explicit canonical_t() = default;
};
inline constexpr canonical_t canonical{};

}  // namespace knot

namespace knot::detail {

// How deep a value the type does not have may nest before it is refused.
inline constexpr int deepest_passed_over = 128;

template <class Iterator, class Sentinel>
class cursor {
 public:
  constexpr cursor(Iterator at, Sentinel end)
      : at_(std::move(at)), end_(std::move(end)) {}

  // The next character as a byte, or -1 at the end.
  [[nodiscard]] constexpr int peek() const {
    return at_ == end_ ? -1 : static_cast<unsigned char>(*at_);
  }
  constexpr void next() {
    ++at_;
    ++offset_;
  }
  [[nodiscard]] constexpr bool at_end() const { return at_ == end_; }
  [[nodiscard]] constexpr std::size_t offset() const { return offset_; }

  // Where the text is in memory: what is left of it, to be read in runs, and
  // a step over as much of it as was.
  static constexpr bool in_memory =
      std::contiguous_iterator<Iterator> && std::sized_sentinel_for<Sentinel, Iterator>;
  [[nodiscard]] constexpr std::string_view rest() const
    requires in_memory
  {
    return {std::to_address(at_), static_cast<std::size_t>(end_ - at_)};
  }
  constexpr void skip(std::size_t count)
    requires in_memory
  {
    at_ += static_cast<std::ptrdiff_t>(count);
    offset_ += count;
  }

  // The first failure is the one kept: everything after it is its consequence.
  constexpr bool fail(std::string_view why) { return fail_at(why, offset_); }
  constexpr bool fail_at(std::string_view why, std::size_t at) {
    if (!failure_) failure_ = error{why, at};
    return false;
  }
  [[nodiscard]] constexpr const std::optional<error>& failure() const {
    return failure_;
  }

  constexpr bool expect(char wanted, std::string_view why) {
    if (peek() != static_cast<unsigned char>(wanted)) return fail(why);
    next();
    return true;
  }
  constexpr bool literal(std::string_view wanted, std::string_view why) {
    for (const char letter : wanted) {
      if (!expect(letter, why)) return false;
    }
    return true;
  }

 private:
  Iterator at_;
  [[no_unique_address]] Sentinel end_;
  std::size_t offset_ = 0;
  std::optional<error> failure_;

 public:
  // How deep the value being read nests, where no type bounds it.
  int depth = 0;
};

// White space, where ordinary JSON allows it.
template <bool Canonical, class Cursor>
constexpr void space(Cursor& in) {
  if constexpr (!Canonical) {
    for (int letter = in.peek();
         letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r';
         letter = in.peek()) {
      in.next();
    }
  }
}

template <class Cursor>
constexpr int hex_digit(Cursor& in, bool upper_too) {
  const int letter = in.peek();
  if (letter >= '0' && letter <= '9') return letter - '0';
  if (letter >= 'a' && letter <= 'f') return letter - 'a' + 10;
  if (upper_too && letter >= 'A' && letter <= 'F') return letter - 'A' + 10;
  return -1;
}

// Four hexadecimal digits, after "\u".
template <class Cursor>
constexpr int four_hex(Cursor& in) {
  int value = 0;
  for (int at = 0; at != 4; ++at) {
    const int digit = hex_digit(in, true);
    if (digit < 0) return -1;
    value = value * 16 + digit;
    in.next();
  }
  return value;
}

template <class Sink>
constexpr void append_utf8(Sink& out, std::uint32_t point) {
  if (point < 0x80) {
    out(static_cast<char>(point));
  } else if (point < 0x800) {
    out(static_cast<char>(0xc0 | (point >> 6)));
    out(static_cast<char>(0x80 | (point & 0x3f)));
  } else if (point < 0x10000) {
    out(static_cast<char>(0xe0 | (point >> 12)));
    out(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
    out(static_cast<char>(0x80 | (point & 0x3f)));
  } else {
    out(static_cast<char>(0xf0 | (point >> 18)));
    out(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
    out(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
    out(static_cast<char>(0x80 | (point & 0x3f)));
  }
}

// What follows a backslash in ordinary JSON: any escape there is, a \u that
// names a surrogate taken with its pair.
template <class Cursor, class Sink>
constexpr bool ordinary_escape(Cursor& in, Sink& out) {
  constexpr std::string_view bad = "knot: not an escape";
  constexpr std::string_view lone = "knot: half of a surrogate pair";
  const int escaped = in.peek();
  switch (escaped) {
    case '"': out('"'); break;
    case '\\': out('\\'); break;
    case '/': out('/'); break;
    case 'b': out('\b'); break;
    case 'f': out('\f'); break;
    case 'n': out('\n'); break;
    case 'r': out('\r'); break;
    case 't': out('\t'); break;
    case 'u': {
      const std::size_t start = in.offset() - 1;
      in.next();
      int point = four_hex(in);
      if (point < 0) return in.fail(bad);
      if (point >= 0xdc00 && point <= 0xdfff) return in.fail_at(lone, start);
      if (point >= 0xd800 && point <= 0xdbff) {
        if (in.peek() != '\\') return in.fail_at(lone, start);
        in.next();
        if (in.peek() != 'u') return in.fail_at(lone, start);
        in.next();
        const int low = four_hex(in);
        if (low < 0xdc00 || low > 0xdfff) return in.fail_at(lone, start);
        point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
      }
      append_utf8(out, static_cast<std::uint32_t>(point));
      return true;
    }
    default: return in.fail(bad);
  }
  in.next();
  return true;
}

// What follows a backslash in Canonical JSON: the short forms, and \u00xx in
// lower case only for a control that has none.
template <class Cursor, class Sink>
constexpr bool canonical_escape(Cursor& in, Sink& out) {
  constexpr std::string_view bad = "knot: an escape Canonical JSON does not write";
  const int escaped = in.peek();
  switch (escaped) {
    case '"': out('"'); break;
    case '\\': out('\\'); break;
    case 'b': out('\b'); break;
    case 'f': out('\f'); break;
    case 'n': out('\n'); break;
    case 'r': out('\r'); break;
    case 't': out('\t'); break;
    case 'u': {
      in.next();
      if (!in.literal("00", bad)) return false;
      const int high = hex_digit(in, false);
      if (high != 0 && high != 1) return in.fail(bad);
      in.next();
      const int low = hex_digit(in, false);
      if (low < 0) return in.fail(bad);
      in.next();
      const int value = high * 16 + low;
      if (value == '\b' || value == '\f' || value == '\n' || value == '\r' ||
          value == '\t') {
        return in.fail(bad);
      }
      out(static_cast<char>(value));
      return true;
    }
    default: return in.fail(bad);
  }
  in.next();
  return true;
}

// A string, each byte of it as it is decoded handed to a sink. The same UTF-8
// either way; the escapes are what differ.
template <bool Canonical, class Cursor, class Sink>
constexpr bool read_string_into(Cursor& in, Sink&& out) {
  if (!in.expect('"', "knot: expected a string")) return false;
  for (;;) {
    const int letter = in.peek();
    if (letter < 0) return in.fail("knot: a string that does not end");
    if (letter == '"') {
      in.next();
      return true;
    }
    if (letter < 0x20) return in.fail("knot: a control character unescaped");
    if (letter == '\\') {
      in.next();
      const bool escaped = Canonical ? canonical_escape(in, out)
                                     : ordinary_escape(in, out);
      if (!escaped) return false;
      continue;
    }
    if (letter < 0x80) {
      if constexpr (std::remove_cvref_t<Cursor>::in_memory &&
                    requires { out.append(std::string_view()); }) {
        // A run of what needs nothing done to it: up to a quote, a backslash,
        // a control or a byte past ASCII, handed over in one piece.
        const std::string_view left = in.rest();
        std::size_t run = 1;
        while (run != left.size()) {
          const auto byte = static_cast<unsigned char>(left[run]);
          if (byte < 0x20 || byte >= 0x80 || byte == '"' || byte == '\\') break;
          ++run;
        }
        out.append(left.substr(0, run));
        in.skip(run);
      } else {
        out(static_cast<char>(letter));
        in.next();
      }
      continue;
    }
    // UTF-8: how many bytes follow, and the range the first of them is in --
    // which is what rules out the overlong, the surrogates and what lies past
    // U+10FFFF.
    int more = 0;
    int low = 0x80;
    int high = 0xbf;
    if (letter >= 0xc2 && letter <= 0xdf) {
      more = 1;
    } else if (letter == 0xe0) {
      more = 2;
      low = 0xa0;
    } else if (letter == 0xed) {
      more = 2;
      high = 0x9f;
    } else if (letter >= 0xe1 && letter <= 0xef) {
      more = 2;
    } else if (letter == 0xf0) {
      more = 3;
      low = 0x90;
    } else if (letter >= 0xf1 && letter <= 0xf3) {
      more = 3;
    } else if (letter == 0xf4) {
      more = 3;
      high = 0x8f;
    } else {
      return in.fail("knot: not UTF-8");
    }
    out(static_cast<char>(letter));
    in.next();
    for (int at = 0; at != more; ++at) {
      const int following = in.peek();
      if (following < low || following > high) {
        return in.fail("knot: not UTF-8");
      }
      out(static_cast<char>(following));
      in.next();
      low = 0x80;
      high = 0xbf;
    }
  }
}

// A sink that is a string: a byte, or a run of them.
struct string_sink {
  std::string& out;
  constexpr void operator()(char letter) { out += letter; }
  constexpr void append(std::string_view run) { out.append(run); }
};

// A sink that keeps nothing, for what is passed over.
struct no_sink {
  constexpr void operator()(char) {}
  constexpr void append(std::string_view) {}
};

template <bool Canonical, class Cursor>
constexpr bool read_string(Cursor& in, std::string& out) {
  out.clear();
  return read_string_into<Canonical>(in, string_sink{out});
}

inline constexpr std::int64_t most_integer = (std::int64_t{1} << 53) - 1;

// A number in ordinary JSON: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?,
// taken as the whole number it is, or refused if it is not one. The digits
// are kept as they come, so 1.0, 10e-1 and 0.1e1 are all 1, and nothing is
// ever rounded.
template <class Cursor>
constexpr bool ordinary_number(Cursor& in, bool& negative,
                               std::int64_t& magnitude) {
  constexpr std::string_view bad = "knot: not a number";
  constexpr std::string_view fraction = "knot: not a whole number";
  constexpr std::string_view range = "knot: a number outside what Matrix allows";
  const std::size_t start = in.offset();
  negative = in.peek() == '-';
  if (negative) in.next();
  // The significant digits, with no leading zeros, and the power of ten they
  // are to be multiplied by.
  std::string digits;
  std::int64_t power = 0;
  const auto digit = [&] { return in.peek() >= '0' && in.peek() <= '9'; };
  if (!digit()) return in.fail(bad);
  if (in.peek() == '0') {
    in.next();
  } else {
    while (digit()) {
      digits += static_cast<char>(in.peek());
      in.next();
    }
  }
  if (in.peek() == '.') {
    in.next();
    if (!digit()) return in.fail(bad);
    while (digit()) {
      if (!digits.empty() || in.peek() != '0') {
        digits += static_cast<char>(in.peek());
      }
      --power;
      in.next();
    }
  }
  if (in.peek() == 'e' || in.peek() == 'E') {
    in.next();
    bool down = false;
    if (in.peek() == '+' || in.peek() == '-') {
      down = in.peek() == '-';
      in.next();
    }
    if (!digit()) return in.fail(bad);
    std::int64_t exponent = 0;
    while (digit()) {
      // Past a million the answer is the same: out of range, or zero.
      if (exponent < 1'000'000) exponent = exponent * 10 + (in.peek() - '0');
      in.next();
    }
    power += down ? -exponent : exponent;
  }
  // Zeros at the end of the digits are powers of ten.
  while (!digits.empty() && digits.back() == '0') {
    digits.pop_back();
    ++power;
  }
  if (digits.empty()) {
    magnitude = 0;
    return true;
  }
  if (power < 0) return in.fail_at(fraction, start);
  if (static_cast<std::int64_t>(digits.size()) + power > 16) {
    return in.fail_at(range, start);
  }
  magnitude = 0;
  for (const char letter : digits) magnitude = magnitude * 10 + (letter - '0');
  for (std::int64_t at = 0; at != power; ++at) magnitude *= 10;
  if (magnitude > most_integer) return in.fail_at(range, start);
  return true;
}

// An integer in Canonical JSON: no sign but a minus, no leading zero, no
// "-0", nothing after the digits.
template <class Cursor>
constexpr bool canonical_number(Cursor& in, bool& negative,
                                std::int64_t& magnitude) {
  negative = in.peek() == '-';
  if (negative) in.next();
  const int first = in.peek();
  if (first < '0' || first > '9') return in.fail("knot: expected a number");
  magnitude = first - '0';
  in.next();
  if (first == '0') {
    if (negative) return in.fail("knot: -0 is not canonical");
    return true;
  }
  for (int letter = in.peek(); letter >= '0' && letter <= '9';
       letter = in.peek()) {
    magnitude = magnitude * 10 + (letter - '0');
    if (magnitude > most_integer) {
      return in.fail("knot: an integer outside what Canonical JSON allows");
    }
    in.next();
  }
  return true;
}

template <bool Canonical, class Integer, class Cursor>
constexpr bool read_integer(Cursor& in, Integer& out) {
  const std::size_t start = in.offset();
  bool negative = false;
  std::int64_t magnitude = 0;
  const bool read = Canonical ? canonical_number(in, negative, magnitude)
                              : ordinary_number(in, negative, magnitude);
  if (!read) return false;
  const std::int64_t value = negative ? -magnitude : magnitude;
  if (!std::in_range<Integer>(value)) {
    return in.fail_at("knot: an integer that does not fit its member", start);
  }
  out = static_cast<Integer>(value);
  return true;
}

// A value the type does not have, read to its end and let go.
template <bool Canonical, class Cursor>
constexpr bool pass_over(Cursor& in, int depth = 0) {
  if (depth == deepest_passed_over) return in.fail("knot: nested too deep");
  std::string scratch;
  switch (in.peek()) {
    case '"':
      return read_string_into<Canonical>(in, no_sink{});
    case 't':
      return in.literal("true", "knot: not a value");
    case 'f':
      return in.literal("false", "knot: not a value");
    case 'n':
      return in.literal("null", "knot: not a value");
    case '[': {
      in.next();
      space<Canonical>(in);
      if (in.peek() == ']') {
        in.next();
        return true;
      }
      for (;;) {
        if (!pass_over<Canonical>(in, depth + 1)) return false;
        space<Canonical>(in);
        if (in.peek() == ',') {
          in.next();
          space<Canonical>(in);
          continue;
        }
        return in.expect(']', "knot: expected ',' or ']'");
      }
    }
    case '{': {
      in.next();
      space<Canonical>(in);
      if (in.peek() == '}') {
        in.next();
        return true;
      }
      std::string previous;
      for (bool first = true;; first = false) {
        const std::size_t at = in.offset();
        if (!read_string<Canonical>(in, scratch)) return false;
        if constexpr (Canonical) {
          if (!first && !(previous < scratch)) {
            return in.fail_at("knot: keys out of order", at);
          }
          previous = scratch;
        }
        space<Canonical>(in);
        if (!in.expect(':', "knot: expected ':'")) return false;
        space<Canonical>(in);
        if (!pass_over<Canonical>(in, depth + 1)) return false;
        space<Canonical>(in);
        if (in.peek() == ',') {
          in.next();
          space<Canonical>(in);
          continue;
        }
        return in.expect('}', "knot: expected ',' or '}'");
      }
    }
    default: {
      bool negative = false;
      std::int64_t magnitude = 0;
      if constexpr (Canonical) {
        return canonical_number(in, negative, magnitude);
      } else {
        // Any number at all: this one is not ours to judge.
        const auto digit = [&] { return in.peek() >= '0' && in.peek() <= '9'; };
        if (in.peek() == '-') in.next();
        if (!digit()) return in.fail("knot: not a value");
        if (in.peek() == '0') {
          in.next();
        } else {
          while (digit()) in.next();
        }
        if (in.peek() == '.') {
          in.next();
          if (!digit()) return in.fail("knot: not a number");
          while (digit()) in.next();
        }
        if (in.peek() == 'e' || in.peek() == 'E') {
          in.next();
          if (in.peek() == '+' || in.peek() == '-') in.next();
          if (!digit()) return in.fail("knot: not a number");
          while (digit()) in.next();
        }
        return true;
      }
    }
  }
}

template <bool Canonical, class Type, class Cursor>
constexpr bool read_value(Cursor& in, Type& out);

template <class Type>
struct is_by : std::false_type {};
template <name Tag, class... Alternatives>
struct is_by<by<Tag, Alternatives...>> : std::true_type {};

template <class Alternative>
constexpr std::string_view tag_of() {
  if constexpr (std::same_as<Alternative, value>) {
    return {};
  } else {
    return schema_of<Alternative>.tag_name();
  }
}

// Which alternative a tag names: the one whose schema says it, or the
// knot::value that takes the rest, or none.
template <class By>
struct by_alternatives;
template <name Tag, class... Alternatives>
struct by_alternatives<by<Tag, Alternatives...>> {
  static constexpr std::size_t count = sizeof...(Alternatives);
  static constexpr std::size_t fallback = [] {
    std::size_t found = std::variant_npos;
    std::size_t at = 0;
    ((std::same_as<Alternatives, value> ? (found = at, ++at) : ++at), ...);
    return found;
  }();
  static constexpr std::size_t named(std::string_view tag) {
    std::size_t found = std::variant_npos;
    std::size_t at = 0;
    (void)(((!std::same_as<Alternatives, value> && tag_of<Alternatives>() == tag)
                ? (found = at, true)
                : (++at, false)) ||
           ...);
    return found != std::variant_npos ? found : fallback;
  }
};

template <bool Canonical, class Cursor>
constexpr bool read_any(Cursor& in, value& out);
template <bool Canonical, class Cursor>
constexpr bool read_any_number(Cursor& in, value& out);
template <bool Canonical, class By, class Cursor>
constexpr bool read_by(Cursor& in, By& out);
template <class By>
constexpr bool settle_by(By& out, std::string_view tag);


template <bool Canonical, class Element, class Allocator, class Cursor>
constexpr bool read_array(Cursor& in, std::vector<Element, Allocator>& out) {
  if (!in.expect('[', "knot: expected an array")) return false;
  out.clear();
  space<Canonical>(in);
  if (in.peek() == ']') {
    in.next();
    return true;
  }
  for (;;) {
    if (!read_value<Canonical>(in, out.emplace_back())) return false;
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      continue;
    }
    return in.expect(']', "knot: expected ',' or ']'");
  }
}

// The key of a member, by the rank it sorts at.
template <class Type, std::size_t Rank>
inline constexpr std::string_view key_text =
    schema_of<Type>.key_of(order_of<Type>[Rank]);

template <class Type>
inline constexpr std::size_t longest_key = [] {
  std::size_t most = 0;
  for (std::size_t at = 0; at != schema<Type>::size; ++at) {
    most = std::max(most, schema_of<Type>.key_of(at).size());
  }
  return most;
}();

// A key as it is read: its bytes in room for one more than the longest of the
// type's keys, so that a key too long to be one of them is known for what it
// is. Where the whole of a longer key is wanted -- Canonical JSON's order of
// keys the type does not have -- the rest goes into a string, and only then.
template <std::size_t Room>
struct key_buffer {
  std::array<char, Room + 1> bytes{};
  std::size_t size = 0;
  std::string* rest = nullptr;

  constexpr void operator()(char letter) {
    if (size < bytes.size()) {
      bytes[size] = letter;
    } else if (rest) {
      if (size == bytes.size()) rest->assign(bytes.data(), bytes.size());
      *rest += letter;
    }
    ++size;
  }

  constexpr void clear() { size = 0; }

  // The key, where it could be one of the type's.
  [[nodiscard]] constexpr std::optional<std::string_view> short_text() const {
    if (size > Room) return std::nullopt;
    return std::string_view(bytes.data(), size);
  }
  // The whole of it, where rest was given.
  [[nodiscard]] constexpr std::string_view text() const {
    if (size <= bytes.size()) return std::string_view(bytes.data(), size);
    return *rest;
  }
};

// Which of the type's keys this is, by rank, or the number of keys where it is
// none of them: the comparisons written out, one a key, for the compiler to
// turn into a switch on the length and a few loads.
template <class Type, std::size_t... Rank>
constexpr std::size_t rank_of(std::string_view key,
                              std::index_sequence<Rank...>) {
  std::size_t found = sizeof...(Rank);
  (void)((key == key_text<Type, Rank> ? (found = Rank, true) : false) || ...);
  return found;
}

// A member, where a knot::by among them is told its tag if the tag came first.
template <bool Canonical, class Type, std::size_t Rank, class Cursor, std::size_t Size>
constexpr bool read_member(Cursor& in, Type& out, const std::array<bool, Size>& seen) {
  auto& member = boost::pfr::get<order_of<Type>[Rank]>(out);
  using member_type = std::remove_cvref_t<decltype(member)>;
  if constexpr (is_by<member_type>::value) {
    constexpr std::size_t tag =
        rank_of<Type>(member_type::tag_key, std::make_index_sequence<Size>{});
    static_assert(tag != Size, "knot: a knot::by is chosen by a key its type does not have");
    member.reading = {};
    if (seen[tag]) {
      member.reading.chosen = by_alternatives<member_type>::named(
          boost::pfr::get<order_of<Type>[tag]>(out));
    }
    return read_by<Canonical>(in, member);
  } else {
    return read_value<Canonical>(in, member);
  }
}

// After the object: each knot::by made what its tag names.
template <class Type, std::size_t Rank, std::size_t Size>
constexpr bool settle_member(Type& out, const std::array<bool, Size>& seen) {
  auto& member = boost::pfr::get<order_of<Type>[Rank]>(out);
  using member_type = std::remove_cvref_t<decltype(member)>;
  if constexpr (is_by<member_type>::value) {
    constexpr std::size_t tag =
        rank_of<Type>(member_type::tag_key, std::make_index_sequence<Size>{});
    if (!seen[Rank]) return true;
    const std::string_view named =
        seen[tag] ? std::string_view(boost::pfr::get<order_of<Type>[tag]>(out))
                  : std::string_view();
    return settle_by(member, named);
  } else {
    return true;
  }
}

template <bool Canonical, class Type, class Cursor>
constexpr bool read_object(Cursor& in, Type& out) {
  constexpr std::size_t size = schema<Type>::size;
  constexpr std::size_t room = longest_key<Type>;
  if (!in.expect('{', "knot: expected an object")) return false;
  std::array<bool, size> seen{};
  key_buffer<room> key;
  // For Canonical JSON: the rank of the key expected next -- the keys arrive
  // sorted and so are the type's, so a key is that one, one the type does not
  // have that sorts before it, or a sign that it is missing. Only two unknown
  // keys in a row need each other to be sorted: the one before is kept for
  // that, and the whole of a key longer than any of the type's goes into a
  // string, and only then.
  std::size_t next = 0;
  bool previous_unknown = false;
  key_buffer<room> previous;
  std::string key_rest;
  std::string previous_rest;
  if constexpr (Canonical) {
    key.rest = &key_rest;
    previous.rest = &previous_rest;
  }
  space<Canonical>(in);
  if (in.peek() != '}') {
    for (;;) {
      const std::size_t at = in.offset();
      key.clear();
      if (!read_string_into<Canonical>(in, key)) return false;
      std::size_t rank = size;
      if constexpr (Canonical) {
        const std::string_view text = key.text();
        // An optional key that sorts before this one is absent.
        while (next != size && !required_at<Type>[next] &&
               schema_of<Type>.key_of(order_of<Type>[next]) < text) {
          ++next;
        }
        const std::string_view expected =
            next != size ? schema_of<Type>.key_of(order_of<Type>[next])
                         : std::string_view();
        if (next != size && text == expected) {
          rank = next++;
          previous_unknown = false;
        } else if (next == size || text < expected) {
          const bool in_order =
              previous_unknown
                  ? previous.text() < text
                  : next == 0 ||
                        schema_of<Type>.key_of(order_of<Type>[next - 1]) < text;
          if (!in_order) return in.fail_at("knot: keys out of order", at);
          std::swap(key, previous);
          key.rest = &key_rest;
          previous.rest = &previous_rest;
          std::swap(key_rest, previous_rest);
          previous_unknown = true;
        } else {
          return in.fail_at("knot: a key is missing", at);
        }
      } else {
        const std::optional<std::string_view> known = key.short_text();
        if (known) rank = rank_of<Type>(*known, std::make_index_sequence<size>{});
      }
      space<Canonical>(in);
      if (!in.expect(':', "knot: expected ':'")) return false;
      space<Canonical>(in);
      if (rank == size) {
        if (!pass_over<Canonical>(in)) return false;
      } else {
        if (seen[rank]) return in.fail_at("knot: a key twice", at);
        seen[rank] = true;
        const bool member = [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
          bool read = false;
          (void)((rank == Rank
                      ? (read = read_member<Canonical, Type, Rank>(in, out, seen),
                         true)
                      : false) ||
                 ...);
          return read;
        }(std::make_index_sequence<size>{});
        if (!member) return false;
      }
      space<Canonical>(in);
      if (in.peek() == ',') {
        in.next();
        space<Canonical>(in);
        continue;
      }
      break;
    }
  }
  const std::size_t end = in.offset();
  if (!in.expect('}', "knot: expected ',' or '}'")) return false;
  for (std::size_t rank = 0; rank != size; ++rank) {
    if (!seen[rank] && required_at<Type>[rank]) {
      return in.fail_at("knot: a key is missing", end);
    }
  }
  const bool settled = [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    return (true && ... && settle_member<Type, Rank>(out, seen));
  }(std::make_index_sequence<size>{});
  if (!settled) return in.fail_at("knot: content its type does not name", end);
  return true;
}

// An object whose keys are the data: each key read whole, a key twice
// refused, and in Canonical JSON the keys sorted.
// The same into sorted vectors: the keys and the values gathered as they come,
// then handed over whole -- as they are where Canonical JSON sorted them
// already, sorted once where ordinary JSON did not.
template <bool Canonical, class Map, class Cursor>
constexpr bool read_flat_map(Cursor& in, Map& out) {
  const std::size_t start = in.offset();
  if (!in.expect('{', "knot: expected an object")) return false;
  typename Map::key_container_type keys;
  typename Map::mapped_container_type values;
  space<Canonical>(in);
  if (in.peek() != '}') {
    for (;;) {
      const std::size_t at = in.offset();
      std::string& key = keys.emplace_back();
      if (!read_string<Canonical>(in, key)) return false;
      if constexpr (Canonical) {
        if (keys.size() > 1 && !(keys[keys.size() - 2] < key)) {
          return in.fail_at("knot: keys out of order", at);
        }
      }
      space<Canonical>(in);
      if (!in.expect(':', "knot: expected ':'")) return false;
      space<Canonical>(in);
      if (!read_value<Canonical>(in, values.emplace_back())) return false;
      space<Canonical>(in);
      if (in.peek() == ',') {
        in.next();
        space<Canonical>(in);
        continue;
      }
      break;
    }
  }
  if (!in.expect('}', "knot: expected ',' or '}'")) return false;
  if constexpr (!Canonical) {
    std::vector<std::size_t> order(keys.size());
    for (std::size_t at = 0; at != order.size(); ++at) order[at] = at;
    std::ranges::stable_sort(order, {}, [&](std::size_t at) -> const std::string& {
      return keys[at];
    });
    for (std::size_t at = 1; at < order.size(); ++at) {
      if (keys[order[at - 1]] == keys[order[at]]) {
        return in.fail_at("knot: a key twice", start);
      }
    }
    typename Map::key_container_type sorted_keys;
    typename Map::mapped_container_type sorted_values;
    sorted_keys.reserve(keys.size());
    sorted_values.reserve(values.size());
    for (const std::size_t at : order) {
      sorted_keys.push_back(std::move(keys[at]));
      sorted_values.push_back(std::move(values[at]));
    }
    keys = std::move(sorted_keys);
    values = std::move(sorted_values);
  }
  out = Map(std::sorted_unique, std::move(keys), std::move(values));
  return true;
}

template <bool Canonical, class Map, class Cursor>
constexpr bool read_map(Cursor& in, Map& out) {
  if constexpr (is_flat_map<Map>::value) {
    return read_flat_map<Canonical>(in, out);
  }
  if (!in.expect('{', "knot: expected an object")) return false;
  out.clear();
  space<Canonical>(in);
  if (in.peek() == '}') {
    in.next();
    return true;
  }
  std::string key;
  std::string_view previous;
  for (;;) {
    const std::size_t at = in.offset();
    if (!read_string<Canonical>(in, key)) return false;
    if constexpr (Canonical) {
      if (!out.empty() && !(previous < key)) {
        return in.fail_at("knot: keys out of order", at);
      }
    }
    space<Canonical>(in);
    if (!in.expect(':', "knot: expected ':'")) return false;
    space<Canonical>(in);
    const auto [entry, made] = out.try_emplace(std::move(key));
    if (!made) return in.fail_at("knot: a key twice", at);
    previous = entry->first;
    if (!read_value<Canonical>(in, entry->second)) return false;
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      continue;
    }
    return in.expect('}', "knot: expected ',' or '}'");
  }
}

// A number in a value. In Canonical JSON an integer and nothing else; in
// ordinary JSON a whole number within 2^53 is an integer, and anything else a
// double -- the text checked against JSON's grammar first, then converted.
template <bool Canonical, class Cursor>
constexpr bool read_any_number(Cursor& in, value& out) {
  bool negative = false;
  std::int64_t magnitude = 0;
  if constexpr (Canonical) {
    if (!canonical_number(in, negative, magnitude)) return false;
    out = negative ? -magnitude : magnitude;
    return true;
  } else {
    const std::size_t start = in.offset();
    std::string text;
    const auto digit = [&] { return in.peek() >= '0' && in.peek() <= '9'; };
    const auto take = [&] {
      text += static_cast<char>(in.peek());
      in.next();
    };
    if (in.peek() == '-') take();
    if (!digit()) return in.fail("knot: not a value");
    if (in.peek() == '0') {
      take();
    } else {
      while (digit()) take();
    }
    if (in.peek() == '.') {
      take();
      if (!digit()) return in.fail("knot: not a number");
      while (digit()) take();
    }
    if (in.peek() == 'e' || in.peek() == 'E') {
      take();
      if (in.peek() == '+' || in.peek() == '-') take();
      if (!digit()) return in.fail("knot: not a number");
      while (digit()) take();
    }
    // A whole number in range is an integer, whatever its spelling.
    cursor<const char*, const char*> again(text.data(), text.data() + text.size());
    if (ordinary_number(again, negative, magnitude) && again.at_end()) {
      out = negative ? -magnitude : magnitude;
      return true;
    }
    double number = 0;
    const auto made = std::from_chars(text.data(), text.data() + text.size(), number);
    if (made.ec != std::errc{} || !std::isfinite(number)) {
      return in.fail_at("knot: a number too large", start);
    }
    out = number;
    return true;
  }
}

// Any JSON, into a value: the nesting bounded here, since no type bounds it.
template <bool Canonical, class Cursor>
constexpr bool read_any(Cursor& in, value& out) {
  if (in.depth == deepest_passed_over) return in.fail("knot: nested too deep");
  ++in.depth;
  const bool read = [&] {
    switch (in.peek()) {
      case '"': {
        std::string text;
        if (!read_string<Canonical>(in, text)) return false;
        out = std::move(text);
        return true;
      }
      case 't':
        out = true;
        return in.literal("true", "knot: not a value");
      case 'f':
        out = false;
        return in.literal("false", "knot: not a value");
      case 'n':
        out = nullptr;
        return in.literal("null", "knot: not a value");
      case '[': {
        value::array items;
        if (!read_array<Canonical>(in, items)) return false;
        out = std::move(items);
        return true;
      }
      case '{': {
        value::object members;
        if (!read_map<Canonical>(in, members)) return false;
        out = std::move(members);
        return true;
      }
      default:
        return read_any_number<Canonical>(in, out);
    }
  }();
  --in.depth;
  return read;
}

// ---------------------------------------------------------------------------
// Reading that may turn into a tree.
//
// A typed value is read as far as the text fits it. At the first thing that
// does not -- a value of the wrong kind, a key the type does not have, null
// for an optional, a number that is not a whole one in range, a key missing at
// the end -- that level of the reading turns what it has into a tree node,
// moving it, and reads the rest of the value into the tree. A level below that
// turned hands its node up, and the level above turns in its turn. So a tree
// is made only where the typed value did not hold, and never beside it.

enum class went { fit, tree, failed };




// A typed value as a tree, moved.
template <class Type>
constexpr value to_tree(Type&& made);

template <class Type>
constexpr value object_to_tree(Type&& made, const bool* seen) {
  constexpr std::size_t size = schema<std::remove_cvref_t<Type>>::size;
  using plain = std::remove_cvref_t<Type>;
  value::object members;
  [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    (([&] {
       if (seen && !seen[Rank]) return;
       auto& member = boost::pfr::get<order_of<plain>[Rank]>(made);
       using member_type = std::remove_cvref_t<decltype(member)>;
       if constexpr (is_optional<member_type>::value) {
         if (!member) return;
         members.emplace_hint(members.end(), std::string(key_text<plain, Rank>),
                              to_tree(std::move(*member)));
       } else {
         members.emplace_hint(members.end(), std::string(key_text<plain, Rank>),
                              to_tree(std::move(member)));
       }
     }()),
     ...);
  }(std::make_index_sequence<size>{});
  return value(std::move(members));
}

template <class Type>
constexpr value to_tree(Type&& made) {
  using plain = std::remove_cvref_t<Type>;
  if constexpr (std::same_as<plain, value>) {
    return std::move(made);
  } else if constexpr (std::same_as<plain, std::string> || std::same_as<plain, bool>) {
    return value(std::move(made));
  } else if constexpr (json_integer<plain>) {
    return value(static_cast<std::int64_t>(made));
  } else if constexpr (is_optional<plain>::value) {
    if (!made) return value();
    return to_tree(std::move(*made));
  } else if constexpr (is_vector<plain>::value) {
    value::array items;
    items.reserve(made.size());
    for (auto& one : made) items.push_back(to_tree(std::move(one)));
    return value(std::move(items));
  } else if constexpr (is_map<plain>::value) {
    value::object members;
    for (auto&& [key, one] : made) {
      members.emplace_hint(members.end(), std::string(key), to_tree(std::move(one)));
    }
    return value(std::move(members));
  } else if constexpr (is_by<plain>::value) {
    return std::visit([](auto& held) { return to_tree(std::move(held)); },
                      made.data());
  } else if constexpr (described<plain>) {
    return object_to_tree(std::move(made), nullptr);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

// Whether a tree would make a typed value: asked before anything is moved out
// of it, so that a tree that does not fit is kept whole.
template <class Type>
constexpr bool tree_fits(const value& tree) {
  const auto& held = tree.data();
  if constexpr (std::same_as<Type, value>) {
    return true;
  } else if constexpr (std::same_as<Type, std::string> || std::same_as<Type, bool>) {
    return std::holds_alternative<Type>(held);
  } else if constexpr (json_integer<Type>) {
    const auto* one = std::get_if<std::int64_t>(&held);
    return one && std::in_range<Type>(*one);
  } else if constexpr (is_optional<Type>::value) {
    return tree.is_null() || tree_fits<typename Type::value_type>(tree);
  } else if constexpr (is_vector<Type>::value) {
    const auto* items = std::get_if<value::array>(&held);
    if (!items) return false;
    for (const auto& one : *items) {
      if (!tree_fits<typename Type::value_type>(one)) return false;
    }
    return true;
  } else if constexpr (is_map<Type>::value) {
    const auto* members = std::get_if<value::object>(&held);
    if (!members) return false;
    for (const auto& [key, one] : *members) {
      if (!tree_fits<typename Type::mapped_type>(one)) return false;
    }
    return true;
  } else if constexpr (described<Type>) {
    const auto* members = std::get_if<value::object>(&held);
    if (!members) return false;
    return [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
      return (true && ... && [&] {
        using member_type = field_t<Type, order_of<Type>[Rank]>;
        const auto found = members->find(key_text<Type, Rank>);
        if (found == members->end()) return is_optional<member_type>::value;
        return tree_fits<member_type>(found->second);
      }());
    }(std::make_index_sequence<schema<Type>::size>{});
  } else {
    return false;
  }
}

// A tree as a typed value, moved. Only for a tree that fits.
template <class Type>
constexpr bool from_tree(value& tree, Type& out);

template <class Type>
constexpr bool object_from_tree(value::object& members, Type& out) {
  constexpr std::size_t size = schema<Type>::size;
  return [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    return (true && ... && [&] {
      auto& member = boost::pfr::get<order_of<Type>[Rank]>(out);
      using member_type = std::remove_cvref_t<decltype(member)>;
      const auto found = members.find(key_text<Type, Rank>);
      if (found == members.end()) {
        if constexpr (is_optional<member_type>::value) {
          member.reset();
          return true;
        } else {
          return false;
        }
      }
      return from_tree(found->second, member);
    }());
  }(std::make_index_sequence<size>{});
}

template <class Type>
constexpr bool from_tree(value& tree, Type& out) {
  auto& held = tree.data();
  if constexpr (std::same_as<Type, value>) {
    out = std::move(tree);
    return true;
  } else if constexpr (std::same_as<Type, std::string> || std::same_as<Type, bool>) {
    auto* one = std::get_if<Type>(&held);
    if (!one) return false;
    out = std::move(*one);
    return true;
  } else if constexpr (json_integer<Type>) {
    auto* one = std::get_if<std::int64_t>(&held);
    if (!one || !std::in_range<Type>(*one)) return false;
    out = static_cast<Type>(*one);
    return true;
  } else if constexpr (is_optional<Type>::value) {
    if (tree.is_null()) {
      out.reset();
      return true;
    }
    return from_tree(tree, out.emplace());
  } else if constexpr (is_vector<Type>::value) {
    auto* items = std::get_if<value::array>(&held);
    if (!items) return false;
    out.clear();
    for (auto& one : *items) {
      if (!from_tree(one, out.emplace_back())) return false;
    }
    return true;
  } else if constexpr (is_map<Type>::value) {
    auto* members = std::get_if<value::object>(&held);
    if (!members) return false;
    out.clear();
    for (auto&& [key, one] : *members) {
      typename Type::mapped_type made{};
      if (!from_tree(one, made)) return false;
      out.emplace(std::string(key), std::move(made));
    }
    return true;
  } else if constexpr (described<Type>) {
    auto* members = std::get_if<value::object>(&held);
    if (!members) return false;
    return object_from_tree(*members, out);
  } else {
    return false;
  }
}

// What is left of an array or an object whose reading turned into a tree: the
// rest of it read as a tree, after the element or member that turned it.
template <bool Canonical, class Cursor>
constexpr bool rest_of_array(Cursor& in, value::array& items) {
  for (;;) {
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      if (!read_any<Canonical>(in, items.emplace_back())) return false;
      continue;
    }
    return in.expect(']', "knot: expected ',' or ']'");
  }
}

// previous: the last key read, for Canonical JSON's order.
template <bool Canonical, class Cursor>
constexpr bool rest_of_object(Cursor& in, value::object& members,
                              std::string previous) {
  for (;;) {
    space<Canonical>(in);
    if (in.peek() != ',') return in.expect('}', "knot: expected ',' or '}'");
    in.next();
    space<Canonical>(in);
    const std::size_t at = in.offset();
    std::string key;
    if (!read_string<Canonical>(in, key)) return false;
    if constexpr (Canonical) {
      if (!(previous < key)) return in.fail_at("knot: keys out of order", at);
      previous = key;
    }
    space<Canonical>(in);
    if (!in.expect(':', "knot: expected ':'")) return false;
    space<Canonical>(in);
    const auto [entry, made] = members.try_emplace(std::move(key));
    if (!made) return in.fail_at("knot: a key twice", at);
    if (!read_any<Canonical>(in, entry->second)) return false;
  }
}

template <bool Canonical, class Type, class Cursor>
constexpr went read_or_tree(Cursor& in, Type& out, value& tree);

// Anything that is not the kind of value expected: the whole of it a tree.
template <bool Canonical, class Cursor>
constexpr went all_tree(Cursor& in, value& tree) {
  return read_any<Canonical>(in, tree) ? went::tree : went::failed;
}

template <bool Canonical, class Element, class Allocator, class Cursor>
constexpr went array_or_tree(Cursor& in, std::vector<Element, Allocator>& out,
                             value& tree) {
  if (in.peek() != '[') return all_tree<Canonical>(in, tree);
  in.next();
  out.clear();
  space<Canonical>(in);
  if (in.peek() == ']') {
    in.next();
    return went::fit;
  }
  for (;;) {
    value turned;
    const went element = read_or_tree<Canonical>(in, out.emplace_back(), turned);
    if (element == went::failed) return went::failed;
    if (element == went::tree) {
      out.pop_back();
      value::array items;
      items.reserve(out.size() + 1);
      for (auto& one : out) items.push_back(to_tree(std::move(one)));
      items.push_back(std::move(turned));
      if (!rest_of_array<Canonical>(in, items)) return went::failed;
      tree = value(std::move(items));
      return went::tree;
    }
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      continue;
    }
    return in.expect(']', "knot: expected ',' or ']'") ? went::fit : went::failed;
  }
}

template <bool Canonical, class Map, class Cursor>
constexpr went map_or_tree(Cursor& in, Map& out, value& tree) {
  if (in.peek() != '{') return all_tree<Canonical>(in, tree);
  in.next();
  out.clear();
  space<Canonical>(in);
  if (in.peek() == '}') {
    in.next();
    return went::fit;
  }
  std::string previous;
  for (bool first = true;; first = false) {
    const std::size_t at = in.offset();
    std::string key;
    if (!read_string<Canonical>(in, key)) return went::failed;
    if constexpr (Canonical) {
      if (!first && !(previous < key)) {
        in.fail_at("knot: keys out of order", at);
        return went::failed;
      }
    }
    previous = key;
    space<Canonical>(in);
    if (!in.expect(':', "knot: expected ':'")) return went::failed;
    space<Canonical>(in);
    if (out.contains(key)) {
      in.fail_at("knot: a key twice", at);
      return went::failed;
    }
    typename Map::mapped_type one{};
    value turned;
    const went member = read_or_tree<Canonical>(in, one, turned);
    if (member == went::failed) return went::failed;
    if (member == went::tree) {
      value::object members;
      for (auto&& [had, held] : out) {
        members.emplace(std::string(had), to_tree(std::move(held)));
      }
      members.emplace(key, std::move(turned));
      if (!rest_of_object<Canonical>(in, members, previous)) return went::failed;
      tree = value(std::move(members));
      return went::tree;
    }
    out.emplace(std::move(key), std::move(one));
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      continue;
    }
    return in.expect('}', "knot: expected ',' or '}'") ? went::fit : went::failed;
  }
}

// first_key: where the object's '{' and its first key were read already, to
// choose the type by; the reading goes on from the ':' after it.
template <bool Canonical, class Type, class Cursor>
constexpr went object_or_tree(Cursor& in, Type& out, value& tree,
                              std::string* first_key = nullptr) {
  constexpr std::size_t size = schema<Type>::size;
  if (!first_key) {
    if (in.peek() != '{') return all_tree<Canonical>(in, tree);
    in.next();
  }
  std::array<bool, size> seen{};
  // The members read so far and, from here on, the rest of the object: as a
  // tree, with what was read moved into it.
  const auto turn = [&](std::string key, value turned, const std::string& previous) {
    value made = object_to_tree(std::move(out), seen.data());
    auto& members = std::get<value::object>(made.data());
    members.emplace(std::move(key), std::move(turned));
    if (!rest_of_object<Canonical>(in, members, previous)) return went::failed;
    tree = std::move(made);
    return went::tree;
  };
  std::string previous;
  if (!first_key) space<Canonical>(in);
  if (first_key || in.peek() != '}') {
    for (bool first = true;; first = false) {
      const std::size_t at = in.offset();
      std::string key;
      if (first && first_key) {
        key = std::move(*first_key);
      } else if (!read_string<Canonical>(in, key)) {
        return went::failed;
      }
      if constexpr (Canonical) {
        if (!first && !(previous < key)) {
          in.fail_at("knot: keys out of order", at);
          return went::failed;
        }
      }
      previous = key;
      space<Canonical>(in);
      if (!in.expect(':', "knot: expected ':'")) return went::failed;
      space<Canonical>(in);
      const std::size_t rank = rank_of<Type>(key, std::make_index_sequence<size>{});
      if (rank == size) {
        // A key the type does not have: kept, so the rest is a tree.
        value turned;
        if (!read_any<Canonical>(in, turned)) return went::failed;
        return turn(std::move(key), std::move(turned), previous);
      }
      if (seen[rank]) {
        in.fail_at("knot: a key twice", at);
        return went::failed;
      }
      value turned;
      const went member = [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
        went result = went::failed;
        (void)((rank == Rank
                    ? (result = read_or_tree<Canonical>(
                           in, boost::pfr::get<order_of<Type>[Rank]>(out), turned),
                       true)
                    : false) ||
               ...);
        return result;
      }(std::make_index_sequence<size>{});
      if (member == went::failed) return went::failed;
      if (member == went::tree) return turn(std::move(key), std::move(turned), previous);
      seen[rank] = true;
      space<Canonical>(in);
      if (in.peek() == ',') {
        in.next();
        space<Canonical>(in);
        continue;
      }
      break;
    }
  }
  if (!in.expect('}', "knot: expected ',' or '}'")) return went::failed;
  for (std::size_t rank = 0; rank != size; ++rank) {
    if (!seen[rank] && required_at<Type>[rank]) {
      // Whole, but not this type: what was there, as a tree.
      tree = object_to_tree(std::move(out), seen.data());
      return went::tree;
    }
  }
  return went::fit;
}

template <bool Canonical, class Type, class Cursor>
constexpr went read_or_tree(Cursor& in, Type& out, value& tree) {
  if constexpr (std::same_as<Type, value>) {
    return read_any<Canonical>(in, out) ? went::fit : went::failed;
  } else if constexpr (std::same_as<Type, std::string>) {
    if (in.peek() != '"') return all_tree<Canonical>(in, tree);
    return read_string<Canonical>(in, out) ? went::fit : went::failed;
  } else if constexpr (std::same_as<Type, bool>) {
    if (in.peek() != 't' && in.peek() != 'f') return all_tree<Canonical>(in, tree);
    return read_value<Canonical>(in, out) ? went::fit : went::failed;
  } else if constexpr (json_integer<Type>) {
    if (in.peek() != '-' && (in.peek() < '0' || in.peek() > '9')) {
      return all_tree<Canonical>(in, tree);
    }
    value number;
    if (!read_any_number<Canonical>(in, number)) return went::failed;
    if (const auto* whole = std::get_if<std::int64_t>(&number.data());
        whole && std::in_range<Type>(*whole)) {
      out = static_cast<Type>(*whole);
      return went::fit;
    }
    tree = std::move(number);
    return went::tree;
  } else if constexpr (is_optional<Type>::value) {
    // null is kept by a tree and not by an optional: so it turns.
    if (in.peek() == 'n') return all_tree<Canonical>(in, tree);
    return read_or_tree<Canonical>(in, out.emplace(), tree);
  } else if constexpr (is_vector<Type>::value) {
    return array_or_tree<Canonical>(in, out, tree);
  } else if constexpr (is_map<Type>::value) {
    return map_or_tree<Canonical>(in, out, tree);
  } else if constexpr (is_by<Type>::value) {
    return read_value<Canonical>(in, out) ? went::fit : went::failed;
  } else if constexpr (described<Type>) {
    return object_or_tree<Canonical>(in, out, tree);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

// ---------------------------------------------------------------------------
// knot::by: the content read into one alternative, the tag deciding.

// Which alternative a first key points to, where the tag has not come yet:
// the first typed one that has such a key, or else the knot::value there is
// for what no type has, or else the first.
template <class By>
constexpr std::size_t guessed_by_key(std::string_view key);
template <name Tag, class... Alternatives>
struct guess_of {
  static constexpr std::size_t by_key(std::string_view key) {
    std::size_t found = std::variant_npos;
    std::size_t at = 0;
    (void)(([&] {
             if constexpr (!std::same_as<Alternatives, value>) {
               return rank_of<Alternatives>(
                          key, std::make_index_sequence<schema<Alternatives>::size>{}) !=
                      schema<Alternatives>::size;
             } else {
               return false;
             }
           }()
               ? (found = at, true)
               : (++at, false)) ||
           ...);
    if (found != std::variant_npos) return found;
    constexpr std::size_t fallback =
        by_alternatives<by<Tag, Alternatives...>>::fallback;
    return fallback != std::variant_npos ? fallback : 0;
  }
};
template <name Tag, class... Alternatives>
constexpr std::size_t guessed(const by<Tag, Alternatives...>*, std::string_view key) {
  return guess_of<Tag, Alternatives...>::by_key(key);
}

// The content into one alternative: the one the tag chose, if it came first;
// otherwise the one its first key points to. Either turns into a tree where
// it does not fit.
template <bool Canonical, class By, class Cursor>
constexpr bool read_by(Cursor& in, By& out) {
  using alternatives = by_alternatives<By>;
  auto& state = out.reading;
  state.in_tree = false;
  std::size_t into = state.chosen;
  std::string first_key;
  bool have_key = false;
  if (into == std::variant_npos) {
    into = 0;
    if (in.peek() == '{') {
      in.next();
      space<Canonical>(in);
      if (in.peek() == '}') {
        // Empty: whatever it is, it is that as a tree; the tag makes it more.
        in.next();
        out.data().template emplace<0>();
        state.in_tree = true;
        state.tree = value(value::object{});
        return true;
      }
      if (!read_string<Canonical>(in, first_key)) return false;
      have_key = true;
      into = guessed(static_cast<const By*>(nullptr), first_key);
    }
  }
  return [&]<std::size_t... At>(std::index_sequence<At...>) {
    bool read = false;
    (void)((into == At
                ? (read = [&] {
                     auto& held = out.data().template emplace<At>();
                     using held_type = std::remove_cvref_t<decltype(held)>;
                     value turned;
                     went made = went::failed;
                     if (!have_key) {
                       made = read_or_tree<Canonical>(in, held, turned);
                     } else if constexpr (std::same_as<held_type, value>) {
                       // Read on as a tree from the first key.
                       value::object members;
                       space<Canonical>(in);
                       if (!in.expect(':', "knot: expected ':'")) return false;
                       space<Canonical>(in);
                       auto& one = members[first_key];
                       if (!read_any<Canonical>(in, one)) return false;
                       if (!rest_of_object<Canonical>(in, members, first_key)) return false;
                       held = value(std::move(members));
                       made = went::fit;
                     } else if constexpr (described<held_type>) {
                       made = object_or_tree<Canonical>(in, held, turned, &first_key);
                     } else {
                       // Not an object, though the text is: the whole a tree.
                       value::object members;
                       space<Canonical>(in);
                       if (!in.expect(':', "knot: expected ':'")) return false;
                       space<Canonical>(in);
                       if (!read_any<Canonical>(in, members[first_key])) return false;
                       if (!rest_of_object<Canonical>(in, members, first_key)) return false;
                       turned = value(std::move(members));
                       made = went::tree;
                     }
                     if (made == went::failed) return false;
                     if (made == went::tree) {
                       state.in_tree = true;
                       state.tree = std::move(turned);
                     }
                     return true;
                   }(),
                   true)
                : false) ||
           ...);
    return read;
  }(std::make_index_sequence<alternatives::count>{});
}

// Once the object is read: the content made the alternative its tag names --
// moved over through a tree where it was read into another, or kept as the
// tree where the named one does not fit it.
template <class By>
constexpr bool settle_by(By& out, std::string_view tag) {
  using alternatives = by_alternatives<By>;
  auto& state = out.reading;
  const std::size_t target = alternatives::named(tag);
  if (target == std::variant_npos) return false;
  const std::size_t read_into = out.data().index();
  if (!state.in_tree && read_into == target) return true;
  value tree = state.in_tree
                   ? std::move(state.tree)
                   : std::visit([](auto& held) { return to_tree(std::move(held)); },
                                out.data());
  state.in_tree = false;
  state.tree = value();
  const bool made = [&]<std::size_t... At>(std::index_sequence<At...>) {
    bool fits = false;
    (void)((target == At
                ? (fits = [&] {
                     using typed_type = std::variant_alternative_t<At, typename By::variant>;
                     if (!tree_fits<typed_type>(tree)) return false;
                     typed_type typed{};
                     if (!from_tree(tree, typed)) return false;
                     out.data().template emplace<At>(std::move(typed));
                     return true;
                   }(),
                   true)
                : false) ||
           ...);
    return fits;
  }(std::make_index_sequence<alternatives::count>{});
  if (made) return true;
  if constexpr (alternatives::fallback != std::variant_npos) {
    out.data().template emplace<alternatives::fallback>(std::move(tree));
    return true;
  }
  return false;
}


template <bool Canonical, class Type, class Cursor>
constexpr bool read_value(Cursor& in, Type& out) {
  if constexpr (std::same_as<Type, std::string>) {
    return read_string<Canonical>(in, out);
  } else if constexpr (std::same_as<Type, bool>) {
    if (in.peek() == 't') {
      out = true;
      return in.literal("true", "knot: expected true or false");
    }
    out = false;
    return in.literal("false", "knot: expected true or false");
  } else if constexpr (json_integer<Type>) {
    return read_integer<Canonical>(in, out);
  } else if constexpr (std::same_as<Type, value>) {
    return read_any<Canonical>(in, out);
  } else if constexpr (is_by<Type>::value) {
    out.reading = {};
    return read_by<Canonical>(in, out);
  } else if constexpr (is_optional<Type>::value) {
    // Present, or null: which is what absent is written as by some.
    if (in.peek() == 'n') {
      out.reset();
      return in.literal("null", "knot: not a value");
    }
    return read_value<Canonical>(in, out.emplace());
  } else if constexpr (is_vector<Type>::value) {
    return read_array<Canonical>(in, out);
  } else if constexpr (is_map<Type>::value) {
    return read_map<Canonical>(in, out);
  } else if constexpr (described<Type>) {
    return read_object<Canonical>(in, out);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

template <bool Canonical, class Type, class Iterator, class Sentinel>
constexpr std::expected<Type, error> read_whole(Iterator at, Sentinel end) {
  cursor<Iterator, Sentinel> in(std::move(at), std::move(end));
  Type made{};
  space<Canonical>(in);
  if (read_value<Canonical>(in, made)) {
    space<Canonical>(in);
    if (!in.at_end()) in.fail("knot: something after the document");
  }
  if (in.failure()) return std::unexpected(*in.failure());
  return made;
}

template <class Range>
concept characters =
    std::ranges::input_range<Range> &&
    !std::convertible_to<Range, std::string_view> &&
    std::convertible_to<std::ranges::range_reference_t<Range>, char>;

}  // namespace knot::detail

export namespace knot {

// Any JSON, from text in memory or from any input range of characters, read
// once: a stream, a socket's bytes, a view over pieces.
template <described Type>
constexpr std::expected<Type, error> read(std::string_view text) {
  return detail::read_whole<false, Type>(text.begin(), text.end());
}
template <described Type, detail::characters Range>
constexpr std::expected<Type, error> read(Range&& text) {
  return detail::read_whole<false, Type>(std::ranges::begin(text),
                                         std::ranges::end(text));
}

// A tree as a typed value, moved out of it; nothing, and the tree kept whole,
// where it does not fit.
template <class Type>
constexpr std::optional<Type> from_value(value& tree) {
  if (!detail::tree_fits<Type>(tree)) return std::nullopt;
  Type made{};
  if (!detail::from_tree(tree, made)) return std::nullopt;
  return made;
}

// Canonical JSON and nothing else: what a signature or a hash is taken over.
template <described Type>
constexpr std::expected<Type, error> read(std::string_view text, canonical_t) {
  return detail::read_whole<true, Type>(text.begin(), text.end());
}
template <described Type, detail::characters Range>
constexpr std::expected<Type, error> read(Range&& text, canonical_t) {
  return detail::read_whole<true, Type>(std::ranges::begin(text),
                                        std::ranges::end(text));
}

}  // namespace knot
