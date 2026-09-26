// Documents made up at random, written as ordinary JSON with white space and
// keys in any order, read back into a knot::value and compared with what was
// made; written as Canonical JSON and read back strictly; and then broken a
// byte at a time, which may fail to read but must never do anything worse.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "read_shapes.h"

namespace {

struct maker {
  std::mt19937_64 random;

  std::size_t below(std::size_t bound) { return std::uniform_int_distribution<std::size_t>(0, bound - 1)(random); }

  std::string text() {
    static const std::vector<std::string> pieces{
        "a", "hello", " ", "\"", "\\", "/", "\n", "\t", "\x01", "\x1f", "\xd0\xbf",
        "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xef\xbf\xbd", "{", "}", "[", ":", ",",
        std::string(40, 'x')};
    std::string made;
    for (std::size_t at = below(6); at != 0; --at) made += pieces[below(pieces.size())];
    return made;
  }

  knot::value value(int depth) {
    switch (below(depth > 3 ? 5 : 7)) {
      case 0: return knot::value();
      case 1: return knot::value(below(2) == 1);
      case 2: {
        const std::int64_t most = (std::int64_t{1} << 53) - 1;
        std::uniform_int_distribution<std::int64_t> any(-most, most);
        return knot::value(below(3) == 0 ? any(random) : std::int64_t(below(1000)) - 500);
      }
      case 3: return knot::value(double(std::int64_t(below(2000)) - 1000) + 0.5);
      case 4: return knot::value(text());
      case 5: {
        knot::value::array items;
        for (std::size_t at = below(5); at != 0; --at) items.push_back(value(depth + 1));
        return knot::value(std::move(items));
      }
      default: {
        knot::value::object members;
        for (std::size_t at = below(5); at != 0; --at) members[text()] = value(depth + 1);
        return knot::value(std::move(members));
      }
    }
  }

  // Ordinary JSON: white space anywhere, keys shuffled, escapes chosen at
  // random among those that say the same.
  std::string space() {
    static const std::vector<std::string> spaces{"", "", "", " ", "\n", "\t  ", "\r\n"};
    return spaces[below(spaces.size())];
  }

  void write_string(std::string& out, std::string_view text) {
    out += '"';
    for (const char letter : text) {
      const auto byte = static_cast<unsigned char>(letter);
      if (letter == '"') {
        out += "\\\"";
      } else if (letter == '\\') {
        out += "\\\\";
      } else if (letter == '/' && below(2) == 1) {
        out += "\\/";
      } else if (byte < 0x20 || (byte < 0x80 && below(8) == 0)) {
        out += below(2) == 1 ? std::format("\\u{:04x}", int(byte))
                             : std::format("\\u{:04X}", int(byte));
      } else {
        out += letter;
      }
    }
    out += '"';
  }

  void write(std::string& out, const knot::value& one) {
    out += space();
    std::visit(
        [&](const auto& held) {
          using type = std::remove_cvref_t<decltype(held)>;
          if constexpr (std::same_as<type, std::nullptr_t>) {
            out += "null";
          } else if constexpr (std::same_as<type, bool>) {
            out += held ? "true" : "false";
          } else if constexpr (std::same_as<type, std::int64_t>) {
            if (below(4) == 0) {
              out += std::format("{}.0", held);  // whole, though written with a fraction
            } else {
              out += std::format("{}", held);
            }
          } else if constexpr (std::same_as<type, double>) {
            out += std::format("{}", held);
          } else if constexpr (std::same_as<type, std::string>) {
            write_string(out, held);
          } else if constexpr (std::same_as<type, knot::value::array>) {
            out += '[';
            for (std::size_t at = 0; at != held.size(); ++at) {
              if (at) out += ',';
              write(out, held[at]);
            }
            out += space() + ']';
          } else {
            std::vector<std::size_t> order(held.size());
            for (std::size_t at = 0; at != order.size(); ++at) order[at] = at;
            std::ranges::shuffle(order, random);
            out += '{';
            bool first = true;
            for (const std::size_t at : order) {
              if (!first) out += ',';
              first = false;
              out += space();
              write_string(out, held.keys()[at]);
              out += space() + ':';
              write(out, held.values()[at]);
            }
            out += space() + '}';
          }
        },
        one.data());
    out += space();
  }
};

struct holder {
  knot::value any;
};
consteval auto json_schema(knot::type<holder>) { return knot::schema<holder>(); }

TEST(ReadRandom, MadeUpDocuments) {
  maker made{std::mt19937_64(20260925)};
  for (int round = 0; round != 20000; ++round) {
    const knot::value original = made.value(0);
    std::string text = "{\"any\":";
    made.write(text, original);
    text += "}";
    const auto got = knot::try_read<holder>(text);
    ASSERT_TRUE(got) << got.error().message << " at " << got.error().offset << " in " << text;
    ASSERT_TRUE(got->any == original) << text;

    // The same from pieces, a character at a time.
    if (round % 5 == 0) {
      std::vector<std::string> pieces;
      for (std::size_t at = 0; at < text.size(); at += 3) pieces.push_back(text.substr(at, 3));
      const auto piecewise = knot::try_read<holder>(pieces | std::views::join);
      ASSERT_TRUE(piecewise) << text;
      ASSERT_TRUE(piecewise->any == original) << text;
    }

    // Canonical, where it holds nothing Canonical JSON cannot say.
    const std::string canonical = knot::to_json_string(*got);
    const bool has_fraction = canonical.find('.') != std::string::npos;
    const auto strict = knot::try_read<holder>(canonical, knot::canonical);
    if (!has_fraction) {
      ASSERT_TRUE(strict) << strict.error().message << " at " << strict.error().offset
                          << " in " << canonical;
      ASSERT_TRUE(strict->any == original) << canonical;
      ASSERT_EQ(knot::to_json_string(*strict), canonical);
    }

    // Broken a byte at a time: read or refused, nothing else.
    for (int broken = 0; broken != 8; ++broken) {
      std::string wrong = text;
      wrong[made.below(wrong.size())] = static_cast<char>(made.below(256));
      (void)knot::try_read<holder>(wrong);
      (void)knot::try_read<holder>(wrong, knot::canonical);
      (void)knot::try_read<event>(wrong);
    }
  }
}

}  // namespace
