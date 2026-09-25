// knot::by against the plain way, on events made up at random: the content
// read into a tree and then typed by its tag. The two must agree on which
// alternative it is and on its fields -- and written back as Canonical JSON
// they must be the same text, since knot::by keeps what it did not type.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace shapes {

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

struct relation {
  std::string rel_type;
  std::string event_id;
  friend bool operator==(const relation&, const relation&) = default;
};
consteval auto json_schema(knot::type<relation>) { return knot::schema<relation>(); }

struct rich {
  std::string body;
  std::optional<relation> relates_to;
  std::vector<std::int64_t> numbers;
  friend bool operator==(const rich&, const rich&) = default;
};
consteval auto json_schema(knot::type<rich>) {
  return knot::schema<rich>().member<"relates_to">(knot::key("m.relates_to")).tag("org.example.rich");
}

struct typed_event {
  std::string type;
  knot::by<"type", message, member, rich, knot::value> content;
  std::string event_id;
};
consteval auto json_schema(knot::type<typed_event>) { return knot::schema<typed_event>(); }

struct raw_event {
  std::string type;
  knot::value content;
  std::string event_id;
};
consteval auto json_schema(knot::type<raw_event>) { return knot::schema<raw_event>(); }

}  // namespace shapes

namespace {

using namespace shapes;

struct maker {
  std::mt19937_64 random;
  std::size_t below(std::size_t bound) {
    return std::uniform_int_distribution<std::size_t>(0, bound - 1)(random);
  }

  // A value for a key: mostly of the kind the known types want there, now
  // and then of another.
  std::string text() {
    static const std::vector<std::string> words{"hi", "m.text", "join", "leave", "$x",
                                                "m.thread", "\xd0\xbf\xd1\x80", "a \"q\""};
    return words[below(words.size())];
  }
  std::string json_text() {
    std::string out = "\"";
    for (const char letter : text()) {
      if (letter == '"') out += "\\\"";
      else out += letter;
    }
    return out + "\"";
  }
  std::string anything(int depth = 0) {
    switch (below(depth > 2 ? 4 : 6)) {
      case 0: return json_text();
      case 1: return std::to_string(std::int64_t(below(200)) - 100);
      case 2: return below(2) ? "true" : "false";
      case 3: return "null";
      case 4: {
        std::string out = "[";
        for (std::size_t at = below(4); at != 0; --at) {
          out += anything(depth + 1);
          if (at != 1) out += ',';
        }
        return out + "]";
      }
      default: {
        std::string out = "{";
        const std::size_t count = below(3);
        for (std::size_t at = 0; at != count; ++at) {
          out += "\"k" + std::to_string(at) + "\":" + anything(depth + 1);
          if (at + 1 != count) out += ',';
        }
        return out + "}";
      }
    }
  }
  std::string value_for(std::string_view key) {
    if (below(8) == 0) return anything();
    if (key == "numbers") {
      std::string out = "[";
      for (std::size_t at = below(4); at != 0; --at) {
        out += std::to_string(below(50));
        if (at != 1) out += ',';
      }
      return out + "]";
    }
    if (key == "m.relates_to") {
      std::string out = "{";
      std::vector<std::string> parts{"\"rel_type\":" + json_text(), "\"event_id\":" + json_text()};
      if (below(3) == 0) parts.push_back("\"m.in_reply_to\":{\"event_id\":\"$y\"}");
      if (below(4) == 0) parts.pop_back();
      std::ranges::shuffle(parts, random);
      for (std::size_t at = 0; at != parts.size(); ++at) {
        if (at) out += ',';
        out += parts[at];
      }
      return out + "}";
    }
    return json_text();
  }

  std::string content() {
    static const std::vector<std::string> keys{"body", "msgtype", "membership", "displayname",
                                               "m.relates_to", "numbers", "extra", "format"};
    std::vector<std::string> chosen;
    for (const auto& key : keys) {
      if (below(2) == 0) chosen.push_back(key);
    }
    std::ranges::shuffle(chosen, random);
    std::string out = "{";
    for (std::size_t at = 0; at != chosen.size(); ++at) {
      if (at) out += ',';
      out += "\"" + chosen[at] + "\":" + value_for(chosen[at]);
    }
    return out + "}";
  }

  std::string event() {
    static const std::vector<std::string> tags{"m.room.message", "m.room.member",
                                               "org.example.rich", "org.example.other"};
    std::vector<std::string> parts{"\"type\":\"" + tags[below(tags.size())] + "\"",
                                   "\"content\":" + content(), "\"event_id\":\"$e\""};
    std::ranges::shuffle(parts, random);
    return "{" + parts[0] + "," + parts[1] + "," + parts[2] + "}";
  }
};

// The plain way: the tree, typed by its tag where it fits.
template <class Typed>
bool typed_as(knot::value tree, const typed_event& got) {
  const auto made = knot::from_value<Typed>(tree);
  if (!made) return false;
  return got.content.is<Typed>() && got.content.as<Typed>() == *made;
}

TEST(ByRandom, AgreesWithTheTreeThenTyped) {
  maker made{std::mt19937_64(925)};
  int typed = 0;
  for (int round = 0; round != 20000; ++round) {
    const std::string text = made.event();
    const auto by = knot::read<typed_event>(text);
    const auto plain = knot::read<raw_event>(text);
    ASSERT_EQ(bool(by), bool(plain)) << text;
    if (!by) continue;
    // Which alternative, and its fields.
    const std::string& tag = plain->type;
    bool agreed = false;
    if (tag == "m.room.message") agreed = typed_as<message>(plain->content, *by);
    else if (tag == "m.room.member") agreed = typed_as<member>(plain->content, *by);
    else if (tag == "org.example.rich") agreed = typed_as<rich>(plain->content, *by);
    if (agreed) {
      ++typed;
    } else {
      ASSERT_TRUE(by->content.is<knot::value>()) << text;
      ASSERT_TRUE(by->content.as<knot::value>() == plain->content) << text;
    }
    // Written back: nothing lost -- except a null for an optional member,
    // which a typed member keeps as empty.
    if (text.find("null") == std::string::npos) {
      ASSERT_EQ(knot::to_json_string(*by), knot::to_json_string(*plain)) << text;
    }
  }
  // Enough of each kind to mean something.
  EXPECT_GT(typed, 1000);
}

}  // namespace
