// SPDX-License-Identifier: AGPL-3.0-only
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
import splice;
import boost.pfr;
export import knot.format;
export import knot.value;

export namespace knot {

// What went wrong, and how many characters in.
struct error {
  std::string_view message;
  std::size_t offset = 0;
};

// The same, thrown, by the reads that do not hand it back.
struct read_failure : std::runtime_error {
  explicit read_failure(const error& what)
      : std::runtime_error(std::string(what.message) + " at " + std::to_string(what.offset)),
        where(what) {}
  error where;
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

}  // namespace knot::detail

// Exported for knot.write, which finds runs the same way.
export namespace knot::detail {

// ---------------------------------------------------------------------------
// Runs found 32 bytes at a time, where the text is in memory.
//
// Clang's vector types: a compare of 32 bytes at once, the answers packed into
// a 32-bit mask, the first set bit where the run ends. SSE or AVX on x86, NEON
// on ARM, from the same code. At compile time, and for what is left at the
// end, a byte at a time.

using bytes32 = unsigned char __attribute__((vector_size(32)));
using flags32 = bool __attribute__((ext_vector_type(32)));

inline constexpr std::size_t lane = 32;

[[nodiscard]] inline std::uint32_t mask_of(const auto& flags) {
  return __builtin_bit_cast(std::uint32_t, __builtin_convertvector(flags, flags32));
}

inline void load32(bytes32& chunk, const char* at) {
  std::memcpy(&chunk, at, lane);
}

// Whether a byte of a string is itself and nothing else: printable ASCII, and
// not a quote or a backslash.
[[nodiscard]] constexpr bool plain_byte(unsigned char byte) {
  return byte >= 0x20 && byte < 0x80 && byte != '"' && byte != '\\';
}

// How many bytes from here are plain.
[[nodiscard]] constexpr std::size_t plain_run(std::string_view text) {
  std::size_t done = 0;
  if !consteval {
    while (done + lane <= text.size()) {
      bytes32 chunk;
      load32(chunk, text.data() + done);
      const bytes32 over = chunk - static_cast<unsigned char>(0x20);
      const std::uint32_t stop =
          mask_of(over >= static_cast<unsigned char>(0x60)) |
          mask_of(chunk == static_cast<unsigned char>('"')) |
          mask_of(chunk == static_cast<unsigned char>('\\'));
      if (stop != 0) return done + static_cast<std::size_t>(std::countr_zero(stop));
      done += lane;
    }
  }
  while (done != text.size() && plain_byte(static_cast<unsigned char>(text[done]))) ++done;
  return done;
}

// How many bytes from here belong in a string as they are, UTF-8 or not: up to
// a quote, a backslash or a control.
[[nodiscard]] constexpr std::size_t string_run(std::string_view text) {
  std::size_t done = 0;
  if !consteval {
    while (done + lane <= text.size()) {
      bytes32 chunk;
      load32(chunk, text.data() + done);
      const std::uint32_t stop =
          mask_of(chunk < static_cast<unsigned char>(0x20)) |
          mask_of(chunk == static_cast<unsigned char>('"')) |
          mask_of(chunk == static_cast<unsigned char>('\\'));
      if (stop != 0) return done + static_cast<std::size_t>(std::countr_zero(stop));
      done += lane;
    }
  }
  while (done != text.size()) {
    const auto byte = static_cast<unsigned char>(text[done]);
    if (byte < 0x20 || byte == '"' || byte == '\\') break;
    ++done;
  }
  return done;
}

// UTF-8 checked 16 bytes at a time, the way simdjson does (Keiser and
// Lemire, "Validating UTF-8 in less than one instruction per byte"): each
// byte looked up by its high nibble and by the low nibble of the byte before
// it, and the three answers ANDed, so that every error there is leaves a bit
// set -- plus a check that the second and third bytes after a three- or
// four-byte lead are continuations. The lookups are byte shuffles, which is
// SSSE3 on x86: compiled for it here, and used where the processor has it.
#if defined(__x86_64__) || defined(__i386__)
using sbytes16 = char __attribute__((vector_size(16)));
using bytes16 = unsigned char __attribute__((vector_size(16)));

namespace utf8_errors {
inline constexpr unsigned char too_short = 1 << 0;
inline constexpr unsigned char too_long = 1 << 1;
inline constexpr unsigned char overlong_3 = 1 << 2;
inline constexpr unsigned char too_large = 1 << 3;
inline constexpr unsigned char surrogate = 1 << 4;
inline constexpr unsigned char overlong_2 = 1 << 5;
inline constexpr unsigned char too_large_1000 = 1 << 6;
inline constexpr unsigned char overlong_4 = 1 << 6;
inline constexpr unsigned char two_conts = 1 << 7;
inline constexpr unsigned char carry = too_short | too_long | two_conts;
}  // namespace utf8_errors

[[gnu::target("ssse3")]] inline bytes16 lookup16(bytes16 table, bytes16 index) {
  return (bytes16)__builtin_ia32_pshufb128((sbytes16)table, (sbytes16)index);
}

template <int Shift>
[[gnu::target("ssse3")]] inline bytes16 before(bytes16 input, bytes16 previous) {
  // The bytes 16 - Shift back: the end of the previous block, then this one.
  return (bytes16)__builtin_ia32_palignr128((sbytes16)input, (sbytes16)previous, 16 - Shift);
}

[[gnu::target("ssse3")]] inline bytes16 utf8_block_errors(bytes16 input, bytes16 previous) {
  using namespace utf8_errors;
  const bytes16 prev1 = before<1>(input, previous);
  constexpr bytes16 first_high = {
      too_long, too_long, too_long, too_long, too_long, too_long, too_long, too_long,
      two_conts, two_conts, two_conts, two_conts,
      too_short | overlong_2, too_short, too_short | overlong_3 | surrogate,
      too_short | too_large | too_large_1000 | overlong_4};
  constexpr bytes16 first_low = {
      carry | overlong_3 | overlong_2 | overlong_4, carry | overlong_2, carry, carry,
      carry | too_large, carry | too_large | too_large_1000,
      carry | too_large | too_large_1000, carry | too_large | too_large_1000,
      carry | too_large | too_large_1000, carry | too_large | too_large_1000,
      carry | too_large | too_large_1000, carry | too_large | too_large_1000,
      carry | too_large | too_large_1000, carry | too_large | too_large_1000 | surrogate,
      carry | too_large | too_large_1000, carry | too_large | too_large_1000};
  constexpr bytes16 second_high = {
      too_short, too_short, too_short, too_short, too_short, too_short, too_short, too_short,
      too_long | overlong_2 | two_conts | overlong_3 | too_large_1000 | overlong_4,
      too_long | overlong_2 | two_conts | overlong_3 | too_large,
      too_long | overlong_2 | two_conts | surrogate | too_large,
      too_long | overlong_2 | two_conts | surrogate | too_large,
      too_short, too_short, too_short, too_short};
  const bytes16 special = lookup16(first_high, prev1 >> 4) &
                          lookup16(first_low, prev1 & static_cast<unsigned char>(0x0f)) &
                          lookup16(second_high, input >> 4);
  const bytes16 prev2 = before<2>(input, previous);
  const bytes16 prev3 = before<3>(input, previous);
  const bytes16 third =
      __builtin_elementwise_sub_sat(prev2, bytes16{} + static_cast<unsigned char>(0xe0 - 0x80));
  const bytes16 fourth =
      __builtin_elementwise_sub_sat(prev3, bytes16{} + static_cast<unsigned char>(0xf0 - 0x80));
  const bytes16 must_be_continuation = (third | fourth) & static_cast<unsigned char>(0x80);
  return must_be_continuation ^ special;
}

// Whether all of a run is well-formed UTF-8, ending where a character ends.
[[gnu::target("ssse3")]] inline bool utf8_valid_ssse3(std::string_view text) {
  bytes16 previous = {};
  bytes16 errors = {};
  std::size_t done = 0;
  const auto block = [&](bytes16 input) {
    errors |= utf8_block_errors(input, previous);
    previous = input;
  };
  for (; done + 16 <= text.size(); done += 16) {
    bytes16 input;
    std::memcpy(&input, text.data() + done, 16);
    block(input);
  }
  if (done != text.size()) {
    // The rest, with zeros after it: ASCII, so a character cut short shows.
    bytes16 input = {};
    std::memcpy(&input, text.data() + done, text.size() - done);
    block(input);
  }
  // And a block of zeros, for a character cut short at the very end.
  block(bytes16{});
  return __builtin_reduce_or(errors) == 0;
}

inline bool has_ssse3() {
  static const bool has = __builtin_cpu_supports("ssse3");
  return has;
}
#endif

// How much of a run is well-formed UTF-8, ending where a character ends: all
// of it, or up to the first byte that is not -- which the reading then meets a
// byte at a time, and says where. 32 bytes of ASCII are passed at once.
[[nodiscard]] constexpr std::size_t utf8_prefix(std::string_view text) {
#if defined(__x86_64__) || defined(__i386__)
  if !consteval {
    // Checked whole, 16 bytes at a time; where something is wrong, found
    // again below a character at a time, to say how far it is right.
    // ASCII first, 32 bytes at a time: most text is, and it needs no more.
    std::size_t ascii = 0;
    while (ascii + lane <= text.size()) {
      bytes32 chunk;
      load32(chunk, text.data() + ascii);
      if (mask_of(chunk >= static_cast<unsigned char>(0x80)) != 0) break;
      ascii += lane;
    }
    while (ascii != text.size() && static_cast<unsigned char>(text[ascii]) < 0x80) ++ascii;
    if (ascii == text.size()) return ascii;
    // From the first byte that is not: what went before is ASCII, so the
    // check may start there.
    if (text.size() - ascii >= 16 && has_ssse3() && utf8_valid_ssse3(text.substr(ascii))) {
      return text.size();
    }
  }
#endif
  std::size_t done = 0;
  while (done != text.size()) {
    if !consteval {
      if (done + lane <= text.size()) {
        bytes32 chunk;
        load32(chunk, text.data() + done);
        if (mask_of(chunk >= static_cast<unsigned char>(0x80)) == 0) {
          done += lane;
          continue;
        }
      }
    }
    const auto first = static_cast<unsigned char>(text[done]);
    if (first < 0x80) {
      ++done;
      continue;
    }
    std::size_t more = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xbf;
    if (first >= 0xc2 && first <= 0xdf) {
      more = 1;
    } else if (first == 0xe0) {
      more = 2;
      low = 0xa0;
    } else if (first == 0xed) {
      more = 2;
      high = 0x9f;
    } else if (first >= 0xe1 && first <= 0xef) {
      more = 2;
    } else if (first == 0xf0) {
      more = 3;
      low = 0x90;
    } else if (first >= 0xf1 && first <= 0xf3) {
      more = 3;
    } else if (first == 0xf4) {
      more = 3;
      high = 0x8f;
    } else {
      return done;
    }
    // The whole character has to be in the run.
    if (done + more >= text.size()) return done;
    for (std::size_t at = 1; at <= more; ++at) {
      const auto following = static_cast<unsigned char>(text[done + at]);
      if (following < low || following > high) return done;
      low = 0x80;
      high = 0xbf;
    }
    done += more + 1;
  }
  return done;
}

[[nodiscard]] constexpr bool space_byte(unsigned char byte) {
  return byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t';
}

// How many bytes from here are white space.
[[nodiscard]] constexpr std::size_t space_run(std::string_view text) {
  std::size_t done = 0;
  // Most runs are one space or none: asked of one byte before of a vector.
  if (text.empty() || !space_byte(static_cast<unsigned char>(text[0]))) return 0;
  if !consteval {
    while (done + lane <= text.size()) {
      bytes32 chunk;
      load32(chunk, text.data() + done);
      const std::uint32_t white =
          mask_of(chunk == static_cast<unsigned char>(' ')) |
          mask_of(chunk == static_cast<unsigned char>('\n')) |
          mask_of(chunk == static_cast<unsigned char>('\r')) |
          mask_of(chunk == static_cast<unsigned char>('\t'));
      if (white != 0xffffffffu) return done + static_cast<std::size_t>(std::countr_one(white));
      done += lane;
    }
  }
  while (done != text.size() && space_byte(static_cast<unsigned char>(text[done]))) ++done;
  return done;
}

}  // namespace knot::detail

namespace knot::detail {

// White space, where ordinary JSON allows it.
template <bool Canonical, class Cursor>
constexpr void space(Cursor& in) {
  if constexpr (!Canonical && Cursor::in_memory) {
    in.skip(space_run(in.rest()));
  } else if constexpr (!Canonical) {
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
    if constexpr (std::remove_cvref_t<Cursor>::in_memory &&
                  requires { out.append(std::string_view()); }) {
      const std::string_view left = in.rest();
      const std::size_t whole = utf8_prefix(left.substr(0, string_run(left)));
      if (whole != 0) {
        out.append(left.substr(0, whole));
        in.skip(whole);
      }
    }
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
        const std::size_t run = 1 + plain_run(left.substr(1));
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
    // Most numbers are plain integers: taken as they come, and the digits
    // spelled out only where a fraction or an exponent follows.
    std::array<char, 17> kept{};
    std::size_t count = 0;
    std::int64_t quick = 0;
    while (digit() && count != kept.size()) {
      kept[count++] = static_cast<char>(in.peek());
      quick = quick * 10 + (in.peek() - '0');
      in.next();
    }
    if (count != kept.size() && in.peek() != '.' && in.peek() != 'e' && in.peek() != 'E') {
      if (quick > most_integer) return in.fail_at(range, start);
      magnitude = quick;
      return true;
    }
    digits.assign(kept.data(), count);
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
        if constexpr (Canonical) {
          if (!read_string<Canonical>(in, scratch)) return false;
          if (!first && !(previous < scratch)) {
            return in.fail_at("knot: keys out of order", at);
          }
          previous.swap(scratch);
        } else {
          if (!read_string_into<Canonical>(in, no_sink{})) return false;
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

// Capture only the value being retained. The underlying cursor remains the
// source of positions and errors; input iterators are never copied or rewound.
// In-memory input keeps its span fast path instead of copying byte by byte.
template <class Cursor>
class capturing_cursor {
 public:
  static constexpr bool in_memory = false;
  constexpr capturing_cursor(Cursor& in, std::string& text) : in_(in), text_(text) {}
  constexpr int peek() const { return in_.peek(); }
  constexpr std::size_t offset() const { return in_.offset(); }
  constexpr void next() {
    text_ += static_cast<char>(in_.peek());
    in_.next();
  }
  constexpr bool fail(std::string_view why) { return in_.fail(why); }
  constexpr bool fail_at(std::string_view why, std::size_t at) { return in_.fail_at(why, at); }
  constexpr bool expect(char wanted, std::string_view why) {
    if (peek() != static_cast<unsigned char>(wanted)) return fail(why);
    next();
    return true;
  }
  constexpr bool literal(std::string_view wanted, std::string_view why) {
    for (char letter : wanted) if (!expect(letter, why)) return false;
    return true;
  }

 private:
  Cursor& in_;
  std::string& text_;
};

template <bool Canonical, class Cursor>
constexpr bool capture_raw(Cursor& in, std::string& text) {
  if constexpr (Cursor::in_memory) {
    const std::string_view before = in.rest();
    if (!pass_over<Canonical>(in)) return false;
    text.append(before.data(), before.size() - in.rest().size());
    return true;
  } else {
    capturing_cursor capture(in, text);
    return pass_over<Canonical>(capture);
  }
}

template <bool Canonical, class Type, class Cursor>
constexpr bool read_value(Cursor& in, Type& out);

template <class Type>
struct is_by : std::false_type {};
template <name Tag, class... Alternatives>
struct is_by<tagged<Tag, Alternatives...>> : std::true_type {};

// What takes the content no alternative's tag names: a tree, or the text.
template <class Alternative>
concept fallback_alternative = std::same_as<Alternative, value> || std::same_as<Alternative, raw>;

template <class Alternative>
constexpr std::string_view tag_of() {
  if constexpr (fallback_alternative<Alternative>) {
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
struct by_alternatives<tagged<Tag, Alternatives...>> {
  static constexpr std::size_t count = sizeof...(Alternatives);
  static constexpr std::size_t fallback = [] {
    std::size_t found = std::variant_npos;
    std::size_t at = 0;
    ((fallback_alternative<Alternatives> ? (found = at, ++at) : ++at), ...);
    return found;
  }();
  static constexpr std::size_t named(std::string_view tag) {
    std::size_t found = std::variant_npos;
    std::size_t at = 0;
    (void)(((!fallback_alternative<Alternatives> && tag_of<Alternatives>() == tag)
                ? (found = at, true)
                : (++at, false)) ||
           ...);
    return found != std::variant_npos ? found : fallback;
  }
};

template <bool Canonical, class Cursor>
constexpr bool read_any(Cursor& in, value& out);
template <bool Canonical, class Type, class Iterator, class Sentinel>
constexpr std::expected<Type, error> read_whole(Iterator at, Sentinel end);
template <bool Canonical, class Cursor>
constexpr bool read_any_number(Cursor& in, value& out);
template <bool Canonical, class By, class Cursor>
constexpr bool read_by(Cursor& in, By& out);
template <class By>
constexpr bool settle_by(By& out, std::string_view tag);
// With knot::raw among a tagged's alternatives: no tree, the text kept until
// the tag is known (below, by read_by and settle_by constrained to it).
template <class By>
struct defers : std::false_type {};
template <name Tag, class... Alternatives>
struct defers<tagged<Tag, Alternatives...>>
    : std::bool_constant<(std::same_as<Alternatives, raw> || ...)> {};
template <class By>
concept deferring = defers<By>::value;
template <bool Canonical, class By, class Cursor>
  requires deferring<By>
constexpr bool read_by(Cursor& in, By& out);
template <class By>
  requires deferring<By>
constexpr bool settle_by(By& out, std::string_view tag);


template <bool Canonical, class Element, class Allocator, class Cursor>
constexpr bool read_array(Cursor& in, std::vector<Element, Allocator>& out) {
  if (!in.expect('[', "knot: expected an array")) return false;
  out.clear();
  if (in.peek() != ']') out.reserve(4);
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

  // A run at once, where the string reader has one.
  constexpr void append(std::string_view run) {
    if (size + run.size() <= bytes.size()) {
      std::ranges::copy(run, bytes.data() + size);
      size += run.size();
    } else {
      for (const char letter : run) (*this)(letter);
    }
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

// A member, where a knot::tagged among them is told its tag if the tag came first.
template <bool Canonical, class Type, std::size_t Rank, class Cursor, std::size_t Size>
constexpr bool read_member(Cursor& in, Type& out, const std::array<bool, Size>& seen) {
  auto& member = boost::pfr::get<order_of<Type>[Rank]>(out);
  using member_type = std::remove_cvref_t<decltype(member)>;
  if constexpr (is_by<member_type>::value) {
    constexpr std::size_t tag =
        rank_of<Type>(member_type::tag_key, std::make_index_sequence<Size>{});
    static_assert(tag != Size, "knot: a knot::tagged is chosen by a key its type does not have");
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

// After the object: each knot::tagged made what its tag names.
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

// The rest of an object's keys, kept in its rest member: as a tree, in a
// knot::value; as the text of an object, in a knot::raw -- each value passed
// over and its span copied, never read into anything.
template <bool Canonical, class Cursor, class At>
constexpr bool keep_rest(Cursor& in, value& kept, std::string_view key, const At& at) {
  if (!kept.template is<value::object>()) kept = value(value::object{});
  auto& members = spl::get<value::object>(kept.data());
  const auto [entry, made] = members.try_emplace(std::string(key));
  if (!made) return in.fail_at("knot: a key twice", at);
  return read_any<Canonical>(in, entry->second);
}

constexpr void put_json_text(std::string& out, std::string_view text) {
  constexpr std::string_view hex = "0123456789abcdef";
  out += '"';
  for (const char letter : text) {
    const auto code = static_cast<unsigned char>(letter);
    if (letter == '"' || letter == '\\') {
      out += '\\';
      out += letter;
    } else if (code < 0x20) {
      out += "\\u00";
      out += hex[code >> 4];
      out += hex[code & 15];
    } else {
      out += letter;
    }
  }
  out += '"';
}

template <bool Canonical, class Cursor, class At>
constexpr bool keep_rest(Cursor& in, raw& kept, std::string_view key, const At&) {
  if (kept.text.empty()) kept.text = "{}";
  kept.text.pop_back();
  if (kept.text.size() > 1) kept.text += ',';
  put_json_text(kept.text, key);
  kept.text += ':';
  if (!capture_raw<Canonical>(in, kept.text)) return false;
  kept.text += '}';
  return true;
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
  if constexpr (Canonical || keeps_rest<Type>) {
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
        if constexpr (keeps_rest<Type>) {
          auto& kept = boost::pfr::get<schema_of<Type>.rest_member()>(out);
          const std::string_view text = Canonical ? previous.text() : key.text();
          if (!keep_rest<Canonical>(in, kept, text, at)) return false;
        } else {
          if (!pass_over<Canonical>(in)) return false;
        }
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
    // Room for a few at once, rather than one, then two, then four.
    keys.reserve(4);
    values.reserve(4);
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
  // Ordinary JSON that happens to be sorted, as most producers write it, is
  // handed over as it is too.
  const bool sorted = std::ranges::adjacent_find(keys, std::ranges::greater_equal{}) == keys.end();
  if (!Canonical && !sorted) {
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
// A decimal number as the nearest double -- ties to even -- worked out
// exactly, with integers as long as they need to be: what std::from_chars
// gives, where it cannot be used, while the compiler evaluates. Nothing
// where it is too large for a double. The text is a JSON number.
namespace exact {

// An unsigned integer of any length: 32-bit limbs, the lowest first, no
// zero limb on top.
using big = std::vector<std::uint32_t>;

constexpr void trim(big& n) {
  while (!n.empty() && n.back() == 0) n.pop_back();
}

constexpr void multiply_add(big& n, std::uint32_t by, std::uint32_t add) {
  std::uint64_t carry = add;
  for (auto& limb : n) {
    const std::uint64_t made = std::uint64_t(limb) * by + carry;
    limb = static_cast<std::uint32_t>(made);
    carry = made >> 32;
  }
  if (carry) n.push_back(static_cast<std::uint32_t>(carry));
}

constexpr std::size_t bits(const big& n) {
  if (n.empty()) return 0;
  return (n.size() - 1) * 32 + std::bit_width(n.back());
}

constexpr bool bit(const big& n, std::size_t at) {
  const std::size_t limb = at / 32;
  return limb < n.size() && ((n[limb] >> (at % 32)) & 1u);
}

// Whether any bit below at is set.
constexpr bool any_below(const big& n, std::size_t at) {
  const std::size_t whole = std::min(at / 32, n.size());
  for (std::size_t i = 0; i < whole; ++i)
    if (n[i]) return true;
  if (whole < n.size() && at % 32) return (n[whole] & ((1u << (at % 32)) - 1)) != 0;
  return false;
}

constexpr void shift_left_one(big& n) {
  std::uint32_t carry = 0;
  for (auto& limb : n) {
    const std::uint32_t next = limb >> 31;
    limb = (limb << 1) | carry;
    carry = next;
  }
  if (carry) n.push_back(carry);
}

constexpr bool less(const big& a, const big& b) {
  if (a.size() != b.size()) return a.size() < b.size();
  for (std::size_t i = a.size(); i-- > 0;)
    if (a[i] != b[i]) return a[i] < b[i];
  return false;
}

constexpr void subtract(big& a, const big& b) {
  std::int64_t borrow = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    std::int64_t made = std::int64_t(a[i]) - borrow - (i < b.size() ? std::int64_t(b[i]) : 0);
    borrow = made < 0;
    if (made < 0) made += std::int64_t(1) << 32;
    a[i] = static_cast<std::uint32_t>(made);
  }
  trim(a);
}

// (n << shift) / d, and whether anything was left over: long division, a bit
// at a time.
constexpr big divide(const big& n, std::size_t shift, const big& d, bool& inexact) {
  const std::size_t total = bits(n) + shift;
  big quotient((total + 31) / 32, 0);
  big rest;
  for (std::size_t at = total; at-- > 0;) {
    shift_left_one(rest);
    if (at >= shift && bit(n, at - shift)) {
      if (rest.empty()) rest.push_back(0);
      rest[0] |= 1u;
    }
    if (!less(rest, d)) {
      subtract(rest, d);
      quotient[at / 32] |= 1u << (at % 32);
    }
  }
  trim(quotient);
  inexact = !rest.empty();
  return quotient;
}

// q times two to the -k, a little more where sticky, as the nearest double.
constexpr std::optional<double> nearest(const big& q, std::int64_t k, bool sticky, bool negative) {
  const std::int64_t length = static_cast<std::int64_t>(bits(q));
  const std::int64_t exponent = length - 1 - k;  // of the highest bit
  if (exponent > 1023) return std::nullopt;
  const std::int64_t precision = exponent >= -1022 ? 53 : 53 - (-1022 - exponent);
  std::uint64_t mantissa = 0;
  if (precision >= 0) {
    const std::int64_t drop = length - precision;
    if (drop <= 0) {
      for (std::int64_t at = length; at-- > 0;)
        mantissa = (mantissa << 1) | (bit(q, static_cast<std::size_t>(at)) ? 1 : 0);
      mantissa <<= -drop;
    } else {
      for (std::int64_t at = length; at-- > drop;)
        mantissa = (mantissa << 1) | (bit(q, static_cast<std::size_t>(at)) ? 1 : 0);
      const bool half = bit(q, static_cast<std::size_t>(drop - 1));
      const bool rest = sticky || any_below(q, static_cast<std::size_t>(drop - 1));
      if (half && (rest || (mantissa & 1))) ++mantissa;
    }
  }
  std::uint64_t pattern = 0;
  if (exponent >= -1022) {
    std::int64_t biased = exponent + 1023;
    if (mantissa == (std::uint64_t(1) << 53)) {
      mantissa >>= 1;
      ++biased;
    }
    if (biased >= 2047) return std::nullopt;
    pattern = (std::uint64_t(biased) << 52) | (mantissa & ((std::uint64_t(1) << 52) - 1));
  } else {
    pattern = mantissa;  // a subnormal; rounded up to 2^52, the least normal
  }
  if (negative) pattern |= std::uint64_t(1) << 63;
  return std::bit_cast<double>(pattern);
}

constexpr std::optional<double> parse(std::string_view text) {
  bool negative = false;
  std::size_t at = 0;
  if (at < text.size() && text[at] == '-') {
    negative = true;
    ++at;
  }
  big digits;
  std::int64_t exponent = 0;
  std::size_t significant = 0;
  const auto take = [&](char digit) {
    if (digits.empty() && digit == '0') return;
    multiply_add(digits, 10, static_cast<std::uint32_t>(digit - '0'));
    ++significant;
  };
  for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at) take(text[at]);
  if (at < text.size() && text[at] == '.') {
    for (++at; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at) {
      take(text[at]);
      --exponent;
    }
  }
  if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
    ++at;
    bool down = false;
    if (text[at] == '+' || text[at] == '-') down = text[at++] == '-';
    std::int64_t written = 0;
    for (; at < text.size(); ++at)
      written = std::min<std::int64_t>(written * 10 + (text[at] - '0'), 100000);
    exponent += down ? -written : written;
  }
  if (digits.empty()) return negative ? -0.0 : 0.0;
  const std::int64_t magnitude = static_cast<std::int64_t>(significant) + exponent;  // of the leading digit, +1
  if (magnitude > 310) return std::nullopt;
  if (magnitude < -330) return negative ? -0.0 : 0.0;
  if (exponent >= 0) {
    for (std::int64_t i = 0; i < exponent; ++i) multiply_add(digits, 10, 0);
    return nearest(digits, 0, false, negative);
  }
  big divisor{1};
  for (std::int64_t i = 0; i < -exponent; ++i) multiply_add(divisor, 10, 0);
  // Enough bits in the quotient for 53 of them, the halfway bit and more.
  const std::int64_t shift = std::max<std::int64_t>(
      0, 56 + static_cast<std::int64_t>(bits(divisor)) - static_cast<std::int64_t>(bits(digits)));
  bool inexact = false;
  const big quotient = divide(digits, static_cast<std::size_t>(shift), divisor, inexact);
  return nearest(quotient, shift, inexact, negative);
}

}  // namespace exact

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
    if consteval {
      const auto made = exact::parse(text);
      if (!made) return in.fail_at("knot: a number too large", start);
      number = *made;
    } else {
      const auto made = std::from_chars(text.data(), text.data() + text.size(), number);
      if (made.ec == std::errc::result_out_of_range) {
        // Too small for a double is zero, as the exact reading says; too
        // large is refused, at run time as while the compiler evaluates.
        const auto exactly = exact::parse(text);
        if (!exactly) return in.fail_at("knot: a number too large", start);
        number = *exactly;
      } else if (made.ec != std::errc{} || !std::isfinite(number)) {
        return in.fail_at("knot: a number too large", start);
      }
    }
    out = number;
    return true;
  }
}

// A number read as a double: a whole one is read as an integer, which has
// no -0, so the sign written says which zero it is.
constexpr double as_double(const value& number, bool minus) {
  if (const auto* whole = spl::get_if<std::int64_t>(&number.data()))
    return *whole == 0 && minus ? -0.0 : static_cast<double>(*whole);
  return spl::get<double>(number.data());
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

constexpr void lay(value& tree, value overlay);

template <class Type>
constexpr value object_to_tree(Type&& made, const bool* seen) {
  constexpr std::size_t size = schema<std::remove_cvref_t<Type>>::size;
  using plain = std::remove_cvref_t<Type>;
  // The members are already in the order of their keys: gathered straight
  // into the flat map's two vectors, room for all of them taken once.
  value::object::key_container_type keys;
  value::object::mapped_container_type values;
  keys.reserve(size);
  values.reserve(size);
  value kept;
  [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    (([&] {
       if constexpr (keeps_rest<plain> &&
                     order_of<plain>[Rank] == schema_of<plain>.rest_member()) {
         kept = to_tree(std::move(boost::pfr::get<order_of<plain>[Rank]>(made)));
         return;
       }
       if (seen && !seen[Rank]) return;
       auto& member = boost::pfr::get<order_of<plain>[Rank]>(made);
       using member_type = std::remove_cvref_t<decltype(member)>;
       if constexpr (is_optional<member_type>::value) {
         if (!member) return;
         keys.emplace_back(key_text<plain, Rank>);
         values.push_back(to_tree(std::move(*member)));
       } else {
         keys.emplace_back(key_text<plain, Rank>);
         values.push_back(to_tree(std::move(member)));
       }
     }()),
     ...);
  }(std::make_index_sequence<size>{});
  value tree(value::object(std::sorted_unique, std::move(keys), std::move(values)));
  lay(tree, std::move(kept));
  return tree;
}

template <class Type>
constexpr value to_tree(Type&& made) {
  using plain = std::remove_cvref_t<Type>;
  if constexpr (std::same_as<plain, value>) {
    return std::move(made);
  } else if constexpr (std::same_as<plain, raw>) {
    // Asked for as a tree, where a program still wants one: read now.
    auto tree = read_whole<false, value>(made.text.data(), made.text.data() + made.text.size());
    return tree ? std::move(*tree) : value();
  } else if constexpr (std::same_as<plain, std::string> || std::same_as<plain, bool>) {
    return value(std::move(made));
  } else if constexpr (json_integer<plain>) {
    return value(static_cast<std::int64_t>(made));
  } else if constexpr (std::same_as<plain, double>) {
    return value(made);
  } else if constexpr (is_choice<plain>::value) {
    return value(std::string(choice<plain>::name(made)));
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
    return spl::visit([](auto& held) { return to_tree(std::move(held)); },
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
  } else if constexpr (std::same_as<Type, raw>) {
    // Text is not made back from a tree: raw is only ever read.
    return false;
  } else if constexpr (std::same_as<Type, std::string> || std::same_as<Type, bool>) {
    return spl::holds_alternative<Type>(held);
  } else if constexpr (json_integer<Type>) {
    const auto* one = spl::get_if<std::int64_t>(&held);
    return one && std::in_range<Type>(*one);
  } else if constexpr (std::same_as<Type, double>) {
    return spl::holds_alternative<double>(held) || spl::holds_alternative<std::int64_t>(held);
  } else if constexpr (is_choice<Type>::value) {
    const auto* one = spl::get_if<std::string>(&held);
    return one && (choice<Type>::open || choice<Type>::find(*one) != choice<Type>::count);
  } else if constexpr (is_optional<Type>::value) {
    return tree.is_null() || tree_fits<typename Type::value_type>(tree);
  } else if constexpr (is_vector<Type>::value) {
    const auto* items = spl::get_if<value::array>(&held);
    if (!items) return false;
    for (const auto& one : *items) {
      if (!tree_fits<typename Type::value_type>(one)) return false;
    }
    return true;
  } else if constexpr (is_map<Type>::value) {
    const auto* members = spl::get_if<value::object>(&held);
    if (!members) return false;
    for (const auto& [key, one] : *members) {
      if (!tree_fits<typename Type::mapped_type>(one)) return false;
    }
    return true;
  } else if constexpr (described<Type>) {
    const auto* members = spl::get_if<value::object>(&held);
    if (!members) return false;
    return [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
      return (true && ... && [&] {
        using member_type = field_t<Type, order_of<Type>[Rank]>;
        if constexpr (keeps_rest<Type>) {
          if (order_of<Type>[Rank] == schema_of<Type>.rest_member()) return true;
        }
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
      if constexpr (keeps_rest<Type>) {
        if (order_of<Type>[Rank] == schema_of<Type>.rest_member()) return true;
      }
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
  } else if constexpr (std::same_as<Type, raw>) {
    return false;
  } else if constexpr (std::same_as<Type, std::string> || std::same_as<Type, bool>) {
    auto* one = spl::get_if<Type>(&held);
    if (!one) return false;
    out = std::move(*one);
    return true;
  } else if constexpr (json_integer<Type>) {
    auto* one = spl::get_if<std::int64_t>(&held);
    if (!one || !std::in_range<Type>(*one)) return false;
    out = static_cast<Type>(*one);
    return true;
  } else if constexpr (std::same_as<Type, double>) {
    if (const auto* whole = spl::get_if<std::int64_t>(&held)) {
      out = static_cast<double>(*whole);
      return true;
    }
    const auto* one = spl::get_if<double>(&held);
    if (!one) return false;
    out = *one;
    return true;
  } else if constexpr (is_choice<Type>::value) {
    auto* one = spl::get_if<std::string>(&held);
    return one && choice<Type>::settle(*one, out);
  } else if constexpr (is_optional<Type>::value) {
    if (tree.is_null()) {
      out.reset();
      return true;
    }
    return from_tree(tree, out.emplace());
  } else if constexpr (is_vector<Type>::value) {
    auto* items = spl::get_if<value::array>(&held);
    if (!items) return false;
    out.clear();
    for (auto& one : *items) {
      if (!from_tree(one, out.emplace_back())) return false;
    }
    return true;
  } else if constexpr (is_map<Type>::value) {
    auto* members = spl::get_if<value::object>(&held);
    if (!members) return false;
    out.clear();
    for (auto&& [key, one] : *members) {
      typename Type::mapped_type made{};
      if (!from_tree(one, made)) return false;
      out.emplace(std::string(key), std::move(made));
    }
    return true;
  } else if constexpr (described<Type>) {
    auto* members = spl::get_if<value::object>(&held);
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

// ---------------------------------------------------------------------------
// Keys a type does not have, kept on the side.
//
// Where the reading may yet have to give the content back whole -- the
// content of a knot::tagged, before its tag has decided -- a key the type does not
// have does not end the typed reading: it is kept in an overlay, a tree shaped
// like the content that holds only what the typed value does not. For a
// member the type does have, the overlay holds that member's own overlay; for
// an array, one for each element (null where an element has none). Typed
// value and overlay together are the whole content.

// The overlay laid into a tree: its keys added, its nested overlays laid into
// the members they belong to.
constexpr void lay(value& tree, value overlay) {
  if (overlay.is_null()) return;
  auto* into = spl::get_if<value::object>(&tree.data());
  auto* from = spl::get_if<value::object>(&overlay.data());
  if (into && from) {
    for (auto&& [key, one] : *from) {
      const auto found = into->find(key);
      if (found == into->end()) {
        into->emplace(std::string(key), std::move(one));
      } else {
        lay(found->second, std::move(one));
      }
    }
    return;
  }
  auto* items = spl::get_if<value::array>(&tree.data());
  auto* overlays = spl::get_if<value::array>(&overlay.data());
  if (items && overlays) {
    for (std::size_t at = 0; at < items->size() && at < overlays->size(); ++at) {
      lay((*items)[at], std::move((*overlays)[at]));
    }
  }
}

// What of a tree a type does not have, as that type's overlay: null where
// there is nothing.
template <class Type>
constexpr value left_over(const value& tree) {
  const auto& held = tree.data();
  if constexpr (is_optional<Type>::value) {
    return tree.is_null() ? value() : left_over<typename Type::value_type>(tree);
  } else if constexpr (is_vector<Type>::value) {
    const auto* items = spl::get_if<value::array>(&held);
    if (!items) return value();
    value::array overlays;
    bool any = false;
    for (const auto& one : *items) {
      overlays.push_back(left_over<typename Type::value_type>(one));
      any = any || !overlays.back().is_null();
    }
    return any ? value(std::move(overlays)) : value();
  } else if constexpr (is_map<Type>::value) {
    const auto* members = spl::get_if<value::object>(&held);
    if (!members) return value();
    value::object overlay;
    for (const auto& [key, one] : *members) {
      value inner = left_over<typename Type::mapped_type>(one);
      if (!inner.is_null()) overlay.emplace(std::string(key), std::move(inner));
    }
    return overlay.empty() ? value() : value(std::move(overlay));
  } else if constexpr (described<Type>) {
    const auto* members = spl::get_if<value::object>(&held);
    if (!members) return value();
    value::object overlay;
    for (const auto& [key, one] : *members) {
      const std::size_t rank =
          rank_of<Type>(key, std::make_index_sequence<schema<Type>::size>{});
      if (rank == schema<Type>::size) {
        overlay.emplace(std::string(key), one);
        continue;
      }
      bool null_kept = false;
      value inner = [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
        value found;
        (void)((rank == Rank
                    ? ([&] {
                         using member_type = field_t<Type, order_of<Type>[Rank]>;
                         // A null the optional member reads as empty: kept
                         // here, so that it is written back.
                         if constexpr (is_optional<member_type>::value) {
                           null_kept = one.is_null();
                         }
                         found = left_over<member_type>(one);
                       }(),
                       true)
                    : false) ||
               ...);
        return found;
      }(std::make_index_sequence<schema<Type>::size>{});
      if (!inner.is_null() || null_kept) overlay.emplace(std::string(key), std::move(inner));
    }
    return overlay.empty() ? value() : value(std::move(overlay));
  } else {
    return value();
  }
}

template <bool Canonical, class Type, class Cursor>
constexpr went read_or_tree(Cursor& in, Type& out, value& tree, value* extras);

// Anything that is not the kind of value expected: the whole of it a tree.
template <bool Canonical, class Cursor>
constexpr went all_tree(Cursor& in, value& tree) {
  return read_any<Canonical>(in, tree) ? went::tree : went::failed;
}

template <bool Canonical, class Element, class Allocator, class Cursor>
constexpr went array_or_tree(Cursor& in, std::vector<Element, Allocator>& out,
                             value& tree, value* extras) {
  if (in.peek() != '[') return all_tree<Canonical>(in, tree);
  in.next();
  out.clear();
  value::array overlays;
  bool any = false;
  space<Canonical>(in);
  if (in.peek() == ']') {
    in.next();
    return went::fit;
  }
  for (;;) {
    value turned;
    value overlay;
    const went element = read_or_tree<Canonical>(in, out.emplace_back(), turned,
                                                 extras ? &overlay : nullptr);
    if (element == went::failed) return went::failed;
    if (element == went::tree) {
      out.pop_back();
      value::array items;
      items.reserve(out.size() + 1);
      for (std::size_t at = 0; at != out.size(); ++at) {
        items.push_back(to_tree(std::move(out[at])));
        if (at < overlays.size()) lay(items.back(), std::move(overlays[at]));
      }
      items.push_back(std::move(turned));
      if (!rest_of_array<Canonical>(in, items)) return went::failed;
      tree = value(std::move(items));
      return went::tree;
    }
    any = any || !overlay.is_null();
    overlays.push_back(std::move(overlay));
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      continue;
    }
    if (!in.expect(']', "knot: expected ',' or ']'")) return went::failed;
    if (extras && any) *extras = value(std::move(overlays));
    return went::fit;
  }
}

template <bool Canonical, class Map, class Cursor>
constexpr went map_or_tree(Cursor& in, Map& out, value& tree, value* extras) {
  if (in.peek() != '{') return all_tree<Canonical>(in, tree);
  in.next();
  out.clear();
  value::object overlays;
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
    value overlay;
    const went member = read_or_tree<Canonical>(in, one, turned,
                                                extras ? &overlay : nullptr);
    if (member == went::failed) return went::failed;
    if (member == went::tree) {
      value::object members;
      for (auto&& [had, held] : out) {
        members.emplace(std::string(had), to_tree(std::move(held)));
      }
      value made(std::move(members));
      lay(made, value(std::move(overlays)));
      spl::get<value::object>(made.data()).emplace(key, std::move(turned));
      if (!rest_of_object<Canonical>(in, spl::get<value::object>(made.data()), previous)) {
        return went::failed;
      }
      tree = std::move(made);
      return went::tree;
    }
    if (!overlay.is_null()) overlays.emplace(key, std::move(overlay));
    out.emplace(std::move(key), std::move(one));
    space<Canonical>(in);
    if (in.peek() == ',') {
      in.next();
      space<Canonical>(in);
      continue;
    }
    if (!in.expect('}', "knot: expected ',' or '}'")) return went::failed;
    if (extras && !overlays.empty()) *extras = value(std::move(overlays));
    return went::fit;
  }
}

// first_key: where the object's '{' and its first key were read already, to
// choose the type by; the reading goes on from the ':' after it. extras: where
// the keys the type does not have are kept; without it such a key turns the
// reading into a tree, since nothing else would keep it.
template <bool Canonical, class Type, class Cursor>
constexpr went object_or_tree(Cursor& in, Type& out, value& tree, value* extras,
                              std::string* first_key = nullptr) {
  constexpr std::size_t size = schema<Type>::size;
  if (!first_key) {
    if (in.peek() != '{') return all_tree<Canonical>(in, tree);
    in.next();
  }
  std::array<bool, size> seen{};
  value::object overlay;
  // The members read so far, what was kept beside them and, from here on, the
  // rest of the object: as a tree, with what was read moved into it.
  const auto turn = [&](std::string key, value turned, const std::string& previous) {
    value made = object_to_tree(std::move(out), seen.data());
    lay(made, value(std::move(overlay)));
    auto& members = spl::get<value::object>(made.data());
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
        value kept;
        if (!read_any<Canonical>(in, kept)) return went::failed;
        if (!extras) return turn(std::move(key), std::move(kept), previous);
        const auto [entry, made] = overlay.try_emplace(std::move(key), std::move(kept));
        if (!made) {
          in.fail_at("knot: a key twice", at);
          return went::failed;
        }
      } else {
        if (seen[rank]) {
          in.fail_at("knot: a key twice", at);
          return went::failed;
        }
        value turned;
        value inner;
        bool null_kept = false;
        const went member = [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
          went result = went::failed;
          (void)((rank == Rank
                      ? ([&] {
                           auto& held = boost::pfr::get<order_of<Type>[Rank]>(out);
                           using member_type = std::remove_cvref_t<decltype(held)>;
                           if constexpr (is_optional<member_type>::value) {
                             if (extras && in.peek() == 'n') {
                               held.reset();
                               null_kept = true;
                               result = in.literal("null", "knot: not a value") ? went::fit
                                                                               : went::failed;
                             }
                           }
                         }(),
                         true)
                      : false) ||
                 ...);
          if (null_kept) return result;
          (void)((rank == Rank
                      ? (result = read_or_tree<Canonical>(
                             in, boost::pfr::get<order_of<Type>[Rank]>(out), turned,
                             extras ? &inner : nullptr),
                         true)
                      : false) ||
                 ...);
          return result;
        }(std::make_index_sequence<size>{});
        if (member == went::failed) return went::failed;
        if (member == went::tree) return turn(std::move(key), std::move(turned), previous);
        seen[rank] = true;
        if (!inner.is_null() || null_kept) overlay.emplace(std::move(key), std::move(inner));
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
  if (!in.expect('}', "knot: expected ',' or '}'")) return went::failed;
  for (std::size_t rank = 0; rank != size; ++rank) {
    if (!seen[rank] && required_at<Type>[rank]) {
      // Whole, but not this type: what was there, as a tree.
      tree = object_to_tree(std::move(out), seen.data());
      lay(tree, value(std::move(overlay)));
      return went::tree;
    }
  }
  if (extras && !overlay.empty()) *extras = value(std::move(overlay));
  return went::fit;
}

template <bool Canonical, class Type, class Cursor>
constexpr went read_or_tree(Cursor& in, Type& out, value& tree, value* extras) {
  if constexpr (std::same_as<Type, value>) {
    return read_any<Canonical>(in, out) ? went::fit : went::failed;
  } else if constexpr (std::same_as<Type, raw>) {
    return read_value<Canonical>(in, out) ? went::fit : went::failed;
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
    if (const auto* whole = spl::get_if<std::int64_t>(&number.data());
        whole && std::in_range<Type>(*whole)) {
      out = static_cast<Type>(*whole);
      return went::fit;
    }
    tree = std::move(number);
    return went::tree;
  } else if constexpr (std::same_as<Type, double>) {
    if (in.peek() != '-' && (in.peek() < '0' || in.peek() > '9')) {
      return all_tree<Canonical>(in, tree);
    }
    const bool minus = in.peek() == '-';
    value number;
    if (!read_any_number<Canonical>(in, number)) return went::failed;
    out = as_double(number, minus);
    return went::fit;
  } else if constexpr (is_choice<Type>::value) {
    // A string no alternative is, where none keeps it: a tree, as a number
    // too big for its member is.
    if (in.peek() != '"') return all_tree<Canonical>(in, tree);
    std::string text;
    if (!read_string<Canonical>(in, text)) return went::failed;
    if (choice<Type>::settle(text, out)) return went::fit;
    tree = value(std::move(text));
    return went::tree;
  } else if constexpr (is_optional<Type>::value) {
    // null is kept by a tree and not by an optional: so it turns.
    if (in.peek() == 'n') return all_tree<Canonical>(in, tree);
    return read_or_tree<Canonical>(in, out.emplace(), tree, extras);
  } else if constexpr (is_vector<Type>::value) {
    return array_or_tree<Canonical>(in, out, tree, extras);
  } else if constexpr (is_map<Type>::value) {
    return map_or_tree<Canonical>(in, out, tree, extras);
  } else if constexpr (is_by<Type>::value) {
    return read_value<Canonical>(in, out) ? went::fit : went::failed;
  } else if constexpr (described<Type>) {
    return object_or_tree<Canonical>(in, out, tree, extras);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

// ---------------------------------------------------------------------------
// knot::tagged: the content read into one alternative, the tag deciding.

// Which alternative a first key points to, where the tag has not come yet:
// the first typed one that has such a key, or else the knot::value there is
// for what no type has, or else the first.
template <name Tag, class... Alternatives>
struct guess_of {
  static constexpr std::size_t by_key(std::string_view key) {
    std::size_t found = std::variant_npos;
    std::size_t at = 0;
    (void)(([&] {
             if constexpr (!fallback_alternative<Alternatives>) {
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
        by_alternatives<tagged<Tag, Alternatives...>>::fallback;
    return fallback != std::variant_npos ? fallback : 0;
  }
};
template <name Tag, class... Alternatives>
constexpr std::size_t guessed(const tagged<Tag, Alternatives...>*, std::string_view key) {
  return guess_of<Tag, Alternatives...>::by_key(key);
}

// The content into one alternative: the one the tag chose, if it came first;
// otherwise the one its first key points to. Keys it does not have are kept
// beside it; it turns into a tree where it does not fit.
template <bool Canonical, class By, class Cursor>
constexpr bool read_by(Cursor& in, By& out) {
  using alternatives = by_alternatives<By>;
  auto& state = out.reading;
  state.in_tree = false;
  state.tree = value();
  out.unknown = value();
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
                       made = read_or_tree<Canonical>(in, held, turned, &out.unknown);
                     } else if constexpr (described<held_type>) {
                       made = object_or_tree<Canonical>(in, held, turned, &out.unknown,
                                                        &first_key);
                     } else {
                       // Read on as a tree from the first key.
                       value::object members;
                       space<Canonical>(in);
                       if (!in.expect(':', "knot: expected ':'")) return false;
                       space<Canonical>(in);
                       if (!read_any<Canonical>(in, members[first_key])) return false;
                       if (!rest_of_object<Canonical>(in, members, first_key)) return false;
                       if constexpr (std::same_as<held_type, value>) {
                         held = value(std::move(members));
                         made = went::fit;
                       } else {
                         turned = value(std::move(members));
                         made = went::tree;
                       }
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

// A knot::tagged with knot::raw among its alternatives reads no tree: its
// content's text is kept as it is passed over, and read into the alternative
// the tag names once the tag is known -- straight away, where it came first.
// Content the named alternative does not fit, or that no tag names, stays
// the text, as knot::raw.
template <class By>
  requires deferring<By>
constexpr void settle_text(By& out, std::size_t target, std::string text) {
  using alternatives = by_alternatives<By>;
  const std::size_t into = target == std::variant_npos ? alternatives::fallback : target;
  [&]<std::size_t... At>(std::index_sequence<At...>) {
    (void)((into == At
                ? ([&] {
                     using held_type = spl::variant_alternative_t<At, typename By::variant>;
                     if constexpr (std::same_as<held_type, raw>) {
                       out.data().template emplace<At>(raw{std::move(text)});
                     } else {
                       auto made = read_whole<false, held_type>(text.data(), text.data() + text.size());
                       if (made) {
                         out.data().template emplace<At>(std::move(*made));
                       } else {
                         out.data().template emplace<alternatives::fallback>(raw{std::move(text)});
                       }
                     }
                   }(),
                   true)
                : false) ||
           ...);
  }(std::make_index_sequence<alternatives::count>{});
}

template <bool Canonical, class By, class Cursor>
  requires deferring<By>
constexpr bool read_by(Cursor& in, By& out) {
  auto& state = out.reading;
  out.unknown = value();
  raw kept;
  if (!read_value<Canonical>(in, kept)) return false;
  if (state.chosen != std::variant_npos) {
    settle_text(out, state.chosen, std::move(kept.text));
  } else {
    state.pending = std::move(kept.text);
  }
  return true;
}

template <class By>
  requires deferring<By>
constexpr bool settle_by(By& out, std::string_view tag) {
  auto& state = out.reading;
  if (!state.pending) return true;  // read by its tag already
  settle_text(out, by_alternatives<By>::named(tag), std::move(*state.pending));
  state.pending.reset();
  return true;
}

// One described type made another, member by member, matched by key: the same
// member type moved straight across; a different one made through a tree of
// that member alone; a key only the target has taken from the kept keys; a
// key only the source has left over, with whatever the target does not have.
// Asked first whether it would fit, so that nothing moves where it would not.
template <class From, class To>
constexpr bool hands_over(const From& from, const value& overlay) {
  const auto* kept = spl::get_if<value::object>(&overlay.data());
  return [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    return (true && ... && [&] {
      using to_member = field_t<To, order_of<To>[Rank]>;
      constexpr std::string_view key = key_text<To, Rank>;
      constexpr std::size_t there =
          rank_of<From>(key, std::make_index_sequence<schema<From>::size>{});
      if constexpr (there != schema<From>::size) {
        using from_member = field_t<From, order_of<From>[there]>;
        if constexpr (std::same_as<from_member, to_member>) {
          return true;
        } else {
          value one = to_tree(from_member(boost::pfr::get<order_of<From>[there]>(from)));
          if (kept) {
            if (const auto found = kept->find(key); found != kept->end()) {
              lay(one, found->second);
            }
          }
          return tree_fits<to_member>(one);
        }
      } else {
        if (kept) {
          if (const auto found = kept->find(key); found != kept->end()) {
            return tree_fits<to_member>(found->second);
          }
        }
        return is_optional<to_member>::value;
      }
    }());
  }(std::make_index_sequence<schema<To>::size>{});
}

template <class From, class To>
constexpr void hand_over(From& from, value& overlay, To& to, value& rest) {
  auto* kept = spl::get_if<value::object>(&overlay.data());
  value::object left;
  const auto take = [&](std::string_view key) -> value* {
    if (!kept) return nullptr;
    const auto found = kept->find(key);
    return found == kept->end() ? nullptr : &found->second;
  };
  // What the target has.
  [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    (([&] {
       using to_member = field_t<To, order_of<To>[Rank]>;
       constexpr std::string_view key = key_text<To, Rank>;
       auto& into = boost::pfr::get<order_of<To>[Rank]>(to);
       constexpr std::size_t there =
           rank_of<From>(key, std::make_index_sequence<schema<From>::size>{});
       if constexpr (there != schema<From>::size) {
         using from_member = field_t<From, order_of<From>[there]>;
         auto& had = boost::pfr::get<order_of<From>[there]>(from);
         if constexpr (std::same_as<from_member, to_member>) {
           into = std::move(had);
           if (value* inner = take(key)) {
             left.emplace(std::string(key), std::move(*inner));
           }
         } else {
           value one = to_tree(std::move(had));
           if (value* inner = take(key)) lay(one, std::move(*inner));
           value extra = left_over<to_member>(one);
           from_tree(one, into);
           if (!extra.is_null()) left.emplace(std::string(key), std::move(extra));
         }
       } else if (value* found = take(key)) {
         const bool null_kept = is_optional<to_member>::value && found->is_null();
         value extra = left_over<to_member>(*found);
         from_tree(*found, into);
         if (!extra.is_null() || null_kept) left.emplace(std::string(key), std::move(extra));
       }
     }()),
     ...);
  }(std::make_index_sequence<schema<To>::size>{});
  // What only the source has.
  [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    (([&] {
       constexpr std::string_view key = key_text<From, Rank>;
       if constexpr (rank_of<To>(key, std::make_index_sequence<schema<To>::size>{}) ==
                     schema<To>::size) {
         auto& had = boost::pfr::get<order_of<From>[Rank]>(from);
         using from_member = field_t<From, order_of<From>[Rank]>;
         if constexpr (is_optional<from_member>::value) {
           if (!had) {
             // Empty, but perhaps for a null that was kept: keep it still.
             if (value* inner = take(key)) left.emplace(std::string(key), std::move(*inner));
             return;
           }
         }
         value one = to_tree(std::move(had));
         if (value* inner = take(key)) lay(one, std::move(*inner));
         left.emplace(std::string(key), std::move(one));
       }
     }()),
     ...);
  }(std::make_index_sequence<schema<From>::size>{});
  // And the kept keys neither has.
  if (kept) {
    for (auto&& [key, one] : *kept) {
      if (rank_of<To>(key, std::make_index_sequence<schema<To>::size>{}) != schema<To>::size) continue;
      if (rank_of<From>(key, std::make_index_sequence<schema<From>::size>{}) != schema<From>::size) continue;
      left.emplace(std::string(key), std::move(one));
    }
  }
  rest = left.empty() ? value() : value(std::move(left));
}

// Once the object is read: the content made the alternative its tag names.
// Read into that one, it stays, with what it did not have in unknown. Read
// into another, or turned into a tree, it is made whole again as a tree --
// typed value and kept keys together, by moves -- and the named one taken
// from it, which keeps in unknown whatever it does not have; or, where the
// named one does not fit, the tree stays, as knot::value.
template <class By>
constexpr bool settle_by(By& out, std::string_view tag) {
  using alternatives = by_alternatives<By>;
  auto& state = out.reading;
  const std::size_t target = alternatives::named(tag);
  if (target == std::variant_npos) return false;
  const std::size_t read_into = out.data().index();
  if (!state.in_tree && read_into == target) return true;
  if (!state.in_tree) {
    // From one typed alternative to another: directly, where it fits.
    std::optional<typename By::variant> handed;
    spl::visit(
        [&](auto& held) {
          using from_type = std::remove_cvref_t<decltype(held)>;
          if constexpr (described<from_type>) {
            [&]<std::size_t... At>(std::index_sequence<At...>) {
              (void)((target == At
                          ? ([&] {
                               using to_type =
                                   spl::variant_alternative_t<At, typename By::variant>;
                               if constexpr (described<to_type>) {
                                 if (!hands_over<from_type, to_type>(held, out.unknown)) return;
                                 to_type made{};
                                 value rest;
                                 hand_over(held, out.unknown, made, rest);
                                 handed.emplace(std::in_place_index<At>, std::move(made));
                                 out.unknown = std::move(rest);
                               }
                             }(),
                             true)
                          : false) ||
                     ...);
            }(std::make_index_sequence<alternatives::count>{});
          }
        },
        out.data());
    if (handed) {
      out.data() = std::move(*handed);
      return true;
    }
  }
  value tree;
  if (state.in_tree) {
    tree = std::move(state.tree);
  } else {
    tree = spl::visit([](auto& held) { return to_tree(std::move(held)); }, out.data());
    lay(tree, std::move(out.unknown));
  }
  out.unknown = value();
  state.in_tree = false;
  state.tree = value();
  const bool made = [&]<std::size_t... At>(std::index_sequence<At...>) {
    bool fits = false;
    (void)((target == At
                ? (fits = [&] {
                     using typed_type = spl::variant_alternative_t<At, typename By::variant>;
                     if constexpr (std::same_as<typed_type, value>) {
                       out.data().template emplace<At>(std::move(tree));
                       return true;
                     } else {
                       if (!tree_fits<typed_type>(tree)) return false;
                       value rest = left_over<typed_type>(tree);
                       typed_type typed{};
                       if (!from_tree(tree, typed)) return false;
                       out.data().template emplace<At>(std::move(typed));
                       out.unknown = std::move(rest);
                       return true;
                     }
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
  } else if constexpr (std::same_as<Type, double>) {
    // Any JSON number; Canonical JSON has only integers, so there only those.
    if (in.peek() != '-' && (in.peek() < '0' || in.peek() > '9')) {
      return in.fail("knot: expected a number");
    }
    const bool minus = in.peek() == '-';
    value number;
    if (!read_any_number<Canonical>(in, number)) return false;
    out = as_double(number, minus);
    return true;
  } else if constexpr (is_choice<Type>::value) {
    const std::size_t start = in.offset();
    std::string text;
    if (!read_string<Canonical>(in, text)) return false;
    return choice<Type>::settle(text, out) ||
           in.fail_at("knot: a string none of the choice's alternatives is", start);
  } else if constexpr (std::same_as<Type, value>) {
    return read_any<Canonical>(in, out);
  } else if constexpr (std::same_as<Type, raw>) {
    out.text.clear();
    return capture_raw<Canonical>(in, out.text);
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

// What is read or written whole: a described type, a knot::value, or any of
// what they are made of -- an array, a map, a string, a choice, a number.
template <class Type>
concept document =
    described<Type> || std::same_as<Type, value> || std::same_as<Type, raw> ||
    std::same_as<Type, std::string> ||
    std::same_as<Type, bool> || std::same_as<Type, double> || detail::json_integer<Type> ||
    detail::is_choice<Type>::value || detail::is_vector<Type>::value ||
    detail::is_map<Type>::value;


// Any JSON, from text in memory or from any input range of characters, read
// once: a stream, a socket's bytes, a view over pieces.
template <document Type>
constexpr std::expected<Type, error> try_read(std::string_view text) {
  return detail::read_whole<false, Type>(text.begin(), text.end());
}
template <document Type, detail::characters Range>
constexpr std::expected<Type, error> try_read(Range&& text) {
  return detail::read_whole<false, Type>(std::ranges::begin(text),
                                         std::ranges::end(text));
}

// One described type made another, without going through text: member by
// member, matched by key -- a member of the same type moved across, another
// made from that member alone. What only the source has is left in `rest`,
// where that is asked for. None where the source does not fit the target: a
// member the target needs that the source has not, or one that does not fit.
template <described To, described From>
[[nodiscard]] constexpr std::optional<To> convert(From from, value* rest = nullptr) {
  value none;
  if (!detail::hands_over<From, To>(from, none)) return std::nullopt;
  To made{};
  value left;
  detail::hand_over(from, none, made, left);
  if (rest != nullptr) *rest = std::move(left);
  return made;
}

// The content of a knot::tagged whole, as a tree: the alternative it holds with
// what it did not have laid back in -- what is written, so that an event read
// is written back with nothing lost.
template <class By>
  requires requires(const By& content) { content.unknown; content.data(); }
value as_tree(const By& content) {
  value tree = spl::visit(
      [](const auto& held) {
        auto copy = held;
        return detail::to_tree(std::move(copy));
      },
      content.data());
  detail::lay(tree, content.unknown);
  return tree;
}

// A typed value as a tree -- the rest of its keys, where it keeps them, laid
// back in.
template <class Type>
  requires described<std::remove_cvref_t<Type>>
value to_value(const Type& made) {
  auto copy = made;
  return detail::to_tree(std::move(copy));
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
template <document Type>
constexpr std::expected<Type, error> try_read(std::string_view text, canonical_t) {
  return detail::read_whole<true, Type>(text.begin(), text.end());
}
template <document Type, detail::characters Range>
constexpr std::expected<Type, error> try_read(Range&& text, canonical_t) {
  return detail::read_whole<true, Type>(std::ranges::begin(text),
                                        std::ranges::end(text));
}

// The same, throwing: the value, or a knot::read_failure.
template <document Type>
constexpr Type read(std::string_view text) {
  auto got = try_read<Type>(text);
  if (!got) throw read_failure(got.error());
  return std::move(*got);
}
template <document Type, detail::characters Range>
constexpr Type read(Range&& text) {
  auto got = try_read<Type>(std::forward<Range>(text));
  if (!got) throw read_failure(got.error());
  return std::move(*got);
}
template <document Type>
constexpr Type read(std::string_view text, canonical_t) {
  auto got = try_read<Type>(text, canonical);
  if (!got) throw read_failure(got.error());
  return std::move(*got);
}
template <document Type, detail::characters Range>
constexpr Type read(Range&& text, canonical_t) {
  auto got = try_read<Type>(std::forward<Range>(text), canonical);
  if (!got) throw read_failure(got.error());
  return std::move(*got);
}

}  // namespace knot
