// SPDX-License-Identifier: AGPL-3.0-only
// knot.value: any JSON at all, for what no type describes.
//
//   struct event { std::string type; knot::value content; };
//
// The content of an event of a type nobody here knows, or of one that is read
// before its type is: null, true and false, an integer, a number with a
// fraction or an exponent (ordinary JSON only), a string, an array, an object.
// Objects keep their keys sorted by their bytes, in a std::flat_map, so a value is written back as
// Canonical JSON wherever it holds nothing Canonical JSON cannot say.
export module knot.value;

import std;
export import knot.schema;

export namespace knot {

class value {
 public:
  using array = std::vector<value>;
  // Sorted vectors of keys and of values: in Canonical JSON the keys arrive
  // in order, so an object is appended to and never rearranged.
  using object = std::flat_map<std::string, value, std::less<>>;
  using variant = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               std::string, array, object>;

  // null
  constexpr value() = default;
  constexpr value(std::nullptr_t) {}
  constexpr value(bool held) : held_(held) {}
  template <std::integral Integer>
    requires(!std::same_as<Integer, bool>)
  constexpr value(Integer held) : held_(static_cast<std::int64_t>(held)) {}
  constexpr value(double held) : held_(held) {}
  constexpr value(std::string held) : held_(std::move(held)) {}
  constexpr value(std::string_view held) : held_(std::string(held)) {}
  constexpr value(const char* held) : held_(std::string(held)) {}
  constexpr value(array held) : held_(std::move(held)) {}
  constexpr value(object held) : held_(std::move(held)) {}

  // What it holds, for std::visit, std::get and std::holds_alternative.
  [[nodiscard]] constexpr const variant& data() const& { return held_; }
  [[nodiscard]] constexpr variant& data() & { return held_; }

  template <class Alternative>
  [[nodiscard]] constexpr bool is() const {
    return std::holds_alternative<Alternative>(held_);
  }
  template <class Alternative>
  [[nodiscard]] constexpr const Alternative& as() const {
    return std::get<Alternative>(held_);
  }
  [[nodiscard]] constexpr bool is_null() const { return is<std::nullptr_t>(); }

  // A member of an object, or null where there is none or this is no object.
  [[nodiscard]] const value& operator[](std::string_view key) const {
    static const value none;
    if (const auto* members = std::get_if<object>(&held_)) {
      if (const auto found = members->find(key); found != members->end()) {
        return found->second;
      }
    }
    return none;
  }

  friend bool operator==(const value&, const value&) = default;

 private:
  variant held_;
};

// JSON kept as its text, byte for byte, and never looked into: what a
// program does not understand, held to be shown ("View source") or passed on
// as it came. Read, the value is only passed over to find where it ends;
// written, the text goes out as it is.
struct raw {
  std::string text;
  friend bool operator==(const raw&, const raw&) = default;
};

// A value chosen by a sibling key -- the content of an event by its "type":
//
//   struct message { std::string msgtype; std::string body; };
//   consteval auto json_schema(knot::type<message>) {
//     return knot::schema<message>().tag("m.room.message");
//   }
//   struct room_event {
//     std::string type;
//     knot::tagged<"type", message, member, knot::value> content;
//   };
//
// Each alternative says its tag in its own schema; knot::value last takes what
// no tag names, and what a named alternative does not fit. Only one of them is
// ever made while reading, whichever order "type" and the content come in.
template <name Tag, class... Alternatives>
class tagged {
 public:
  using variant = std::variant<Alternatives...>;
  static constexpr std::string_view tag_key = Tag.view();

  constexpr tagged() = default;
  template <class Alternative>
    requires(std::same_as<std::remove_cvref_t<Alternative>, Alternatives> || ...)
  constexpr tagged(Alternative&& held) : held_(std::forward<Alternative>(held)) {}

  [[nodiscard]] constexpr const variant& data() const& { return held_; }
  [[nodiscard]] constexpr variant& data() & { return held_; }
  template <class Alternative>
  [[nodiscard]] constexpr bool is() const {
    return std::holds_alternative<Alternative>(held_);
  }
  template <class Alternative>
  [[nodiscard]] constexpr const Alternative& as() const {
    return std::get<Alternative>(held_);
  }

  friend constexpr bool operator==(const tagged& one, const tagged& other) {
    return one.held_ == other.held_;
  }

  // Where reading stands, between the content and the tag; nothing of the
  // value itself.
  // What the content had that the alternative held does not: its keys with
  // their values, and for a member it does have, that member's own -- a tree
  // shaped like the content, null where there is nothing.
  value unknown;

  struct reading_state {
    std::size_t chosen = std::variant_npos;  // read into this by its tag
    bool in_tree = false;                    // the content went to the tree
    value tree;
    // With knot::raw among the alternatives: the content's text, kept until
    // the tag says what it is, and then read into that -- no tree between.
    std::optional<std::string> pending;
  } reading;

 private:
  variant held_;
};

}  // namespace knot
