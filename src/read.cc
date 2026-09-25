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
      out(static_cast<char>(letter));
      in.next();
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

template <bool Canonical, class Cursor>
constexpr bool read_string(Cursor& in, std::string& out) {
  out.clear();
  return read_string_into<Canonical>(in, [&](char letter) { out += letter; });
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
      return read_string_into<Canonical>(in, [](char) {});
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
                      ? (read = read_value<Canonical>(
                             in, boost::pfr::get<order_of<Type>[Rank]>(out)),
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
  return true;
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
  } else if constexpr (is_optional<Type>::value) {
    // Present, or null: which is what absent is written as by some.
    if (in.peek() == 'n') {
      out.reset();
      return in.literal("null", "knot: not a value");
    }
    return read_value<Canonical>(in, out.emplace());
  } else if constexpr (is_vector<Type>::value) {
    return read_array<Canonical>(in, out);
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
