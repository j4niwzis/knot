// knot.read: a described type read out of its Canonical JSON.
//
//   const std::expected<event, knot::error> got = knot::read<event>(text);
//
// No automaton is built, because none is needed: for a type known in advance,
// Canonical JSON is decided by the next character everywhere. An object is its
// keys in their one order, each a literal to compare; an array is '[' and then
// ']' or an element, and after each element ',' or ']'; a string is bytes
// checked as they pass; an integer is its digits. So the reader is written
// straight from the type -- one function a type, the nesting the type's own --
// and reads any input range once, a character at a time, looking one ahead.
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

}  // namespace knot

namespace knot::detail {

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

  // The first failure is the one kept: everything after it is its consequence.
  constexpr bool fail(std::string_view why) {
    if (!failure_) failure_ = error{why, offset_};
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

// The key of a member as it is written, with what comes before it and the
// colon after: {"content": for the first, ,"depth": for the rest.
template <class Type, std::size_t Rank>
inline constexpr auto key_literal = freeze<[] {
  std::string out = Rank == 0 ? "{" : ",";
  append_json_string(out, schema_of<Type>.key_of(order_of<Type>[Rank]));
  out += ':';
  return out;
}>();

template <class Cursor>
constexpr int hex_digit(Cursor& in) {
  const int letter = in.peek();
  if (letter >= '0' && letter <= '9') return letter - '0';
  if (letter >= 'a' && letter <= 'f') return letter - 'a' + 10;
  return -1;
}

// A string, exactly as Canonical JSON writes it.
template <class Cursor>
constexpr bool read_string(Cursor& in, std::string& out) {
  constexpr std::string_view bad_escape =
      "knot: an escape Canonical JSON does not write";
  if (!in.expect('"', "knot: expected a string")) return false;
  out.clear();
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
      const int escaped = in.peek();
      switch (escaped) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          // Only a control without a short form, as \u00xx in lower case.
          in.next();
          if (!in.literal("00", bad_escape)) return false;
          const int high = hex_digit(in);
          if (high != 0 && high != 1) return in.fail(bad_escape);
          in.next();
          const int low = hex_digit(in);
          if (low < 0) return in.fail(bad_escape);
          const int value = high * 16 + low;
          if (value == '\b' || value == '\f' || value == '\n' ||
              value == '\r' || value == '\t') {
            return in.fail(bad_escape);
          }
          out += static_cast<char>(value);
          break;
        }
        default: return in.fail(bad_escape);
      }
      in.next();
      continue;
    }
    if (letter < 0x80) {
      out += static_cast<char>(letter);
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
    out += static_cast<char>(letter);
    in.next();
    for (int at = 0; at != more; ++at) {
      const int following = in.peek();
      if (following < low || following > high) {
        return in.fail("knot: not UTF-8");
      }
      out += static_cast<char>(following);
      in.next();
      low = 0x80;
      high = 0xbf;
    }
  }
}

// An integer: no sign but a minus, no leading zero, no "-0", nothing after
// the digits, and no more than 2^53 - 1 either way.
template <class Integer, class Cursor>
constexpr bool read_integer(Cursor& in, Integer& out) {
  constexpr std::int64_t most = (std::int64_t{1} << 53) - 1;
  const bool negative = in.peek() == '-';
  if (negative) {
    if constexpr (std::is_unsigned_v<Integer>) {
      return in.fail("knot: a negative number for a member that cannot be");
    }
    in.next();
  }
  const int first = in.peek();
  if (first < '0' || first > '9') return in.fail("knot: expected a number");
  std::int64_t magnitude = first - '0';
  in.next();
  if (first == '0') {
    if (negative) return in.fail("knot: -0 is not canonical");
  } else {
    for (int letter = in.peek(); letter >= '0' && letter <= '9';
         letter = in.peek()) {
      magnitude = magnitude * 10 + (letter - '0');
      if (magnitude > most) {
        return in.fail("knot: an integer outside what Canonical JSON allows");
      }
      in.next();
    }
  }
  const std::int64_t value = negative ? -magnitude : magnitude;
  if (!std::in_range<Integer>(value)) {
    return in.fail("knot: an integer that does not fit its member");
  }
  out = static_cast<Integer>(value);
  return true;
}

template <class Type, class Cursor>
constexpr bool read_value(Cursor& in, Type& out);

template <class Element, class Allocator, class Cursor>
constexpr bool read_array(Cursor& in, std::vector<Element, Allocator>& out) {
  if (!in.expect('[', "knot: expected an array")) return false;
  out.clear();
  if (in.peek() == ']') {
    in.next();
    return true;
  }
  for (;;) {
    if (!read_value(in, out.emplace_back())) return false;
    if (in.peek() == ',') {
      in.next();
      continue;
    }
    return in.expect(']', "knot: expected ',' or ']'");
  }
}

template <class Type, class Cursor>
constexpr bool read_object(Cursor& in, Type& out) {
  constexpr std::string_view unexpected =
      "knot: not the key that comes next in Canonical JSON";
  const bool members = [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    return (true && ... &&
            (in.literal(key_literal<Type, Rank>.view(), unexpected) &&
             read_value(in, boost::pfr::get<order_of<Type>[Rank]>(out))));
  }(std::make_index_sequence<schema<Type>::size>{});
  if (!members) return false;
  if constexpr (schema<Type>::size == 0) {
    if (!in.expect('{', "knot: expected an object")) return false;
  }
  return in.expect('}', "knot: expected '}'");
}

template <class Type, class Cursor>
constexpr bool read_value(Cursor& in, Type& out) {
  if constexpr (std::same_as<Type, std::string>) {
    return read_string(in, out);
  } else if constexpr (std::same_as<Type, bool>) {
    if (in.peek() == 't') {
      out = true;
      return in.literal("true", "knot: expected true or false");
    }
    out = false;
    return in.literal("false", "knot: expected true or false");
  } else if constexpr (json_integer<Type>) {
    return read_integer(in, out);
  } else if constexpr (is_vector<Type>::value) {
    return read_array(in, out);
  } else if constexpr (described<Type>) {
    return read_object(in, out);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

template <class Type, class Iterator, class Sentinel>
constexpr std::expected<Type, error> read_whole(Iterator at, Sentinel end) {
  cursor<Iterator, Sentinel> in(std::move(at), std::move(end));
  Type made{};
  if (read_value(in, made) && !in.at_end()) {
    in.fail("knot: something after the document");
  }
  if (in.failure()) return std::unexpected(*in.failure());
  return made;
}

}  // namespace knot::detail

export namespace knot {

// From text in memory.
template <described Type>
constexpr std::expected<Type, error> read(std::string_view text) {
  return detail::read_whole<Type>(text.begin(), text.end());
}

// From any input range of characters, read once: a stream, a socket's bytes,
// a view over pieces.
template <described Type, std::ranges::input_range Range>
  requires(!std::convertible_to<Range, std::string_view> &&
           std::convertible_to<std::ranges::range_reference_t<Range>, char>)
constexpr std::expected<Type, error> read(Range&& text) {
  return detail::read_whole<Type>(std::ranges::begin(text),
                                  std::ranges::end(text));
}

}  // namespace knot
