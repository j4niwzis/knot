// SPDX-License-Identifier: AGPL-3.0-only
// knot.schema: what a type is called in JSON.
//
// A type opts in the way chevron's types do, by a function found through
// argument-dependent lookup:
//
//   struct event { std::string content; std::int64_t depth; };
//   consteval auto json_schema(knot::type<event>) {
//     return knot::schema<event>();
//   }
//
// Every member is a key, named as the member is. A key that is not a C++
// name is said by the member it belongs to:
//
//   consteval auto json_schema(knot::type<content>) {
//     return knot::schema<content>()
//         .member<"relates_to">(knot::key("m.relates_to"));
//   }
export module knot.schema;

import std;
import boost.pfr;

export namespace knot {

// The type a schema is asked for by: an empty tag, so that the lookup finds
// the function beside the type.
template <class Type>
struct type {};

// A member's name, said as a template argument.
template <std::size_t Size>
struct name {
  char value[Size]{};
  consteval name(const char (&text)[Size]) { std::copy_n(text, Size, value); }
  [[nodiscard]] constexpr std::string_view view() const {
    return {value, Size - 1};
  }
};

// Where the keys a type does not have go, rather than being passed over: a
// member of type knot::value, said with .member<"extra">(knot::rest).
struct rest_t {
  explicit rest_t() = default;
};
inline constexpr rest_t rest{};

// What a member for the rest has for a key: no key at all -- it is not UTF-8,
// so no key read can ever be it, and it sorts after every one that can.
inline constexpr std::string_view rest_key = "\xff\xff";

// The key a member is written under, where it is not the member's name.
struct key {
  std::string_view text;
  consteval explicit key(std::string_view said) : text(said) {}
};

template <class Type>
class schema {
 public:
  static constexpr std::size_t size = boost::pfr::tuple_size_v<Type>;

  consteval schema() {
    constexpr auto names = boost::pfr::names_as_array<Type>();
    for (std::size_t at = 0; at != size; ++at) keys_[at] = names[at];
  }

  template <name Field>
  [[nodiscard]] consteval schema member(key said) const {
    schema made = *this;
    made.keys_[index_of(Field.view())] = said.text;
    return made;
  }

  template <name Field>
  [[nodiscard]] consteval schema member(rest_t) const {
    schema made = *this;
    made.rest_ = index_of(Field.view());
    made.keys_[made.rest_] = rest_key;
    return made;
  }

  // The member the rest of the keys go to, in declaration order; size where
  // there is none.
  [[nodiscard]] constexpr std::size_t rest_member() const { return rest_; }

  // What names this type where a knot::tagged chooses among several: the value of
  // the sibling key it is chosen by, "m.room.message" for a message.
  [[nodiscard]] consteval schema tag(std::string_view said) const {
    schema made = *this;
    made.tag_ = said;
    return made;
  }
  [[nodiscard]] constexpr std::string_view tag_name() const { return tag_; }

  // The key of the member at a place in declaration order.
  [[nodiscard]] constexpr std::string_view key_of(std::size_t at) const {
    return keys_[at];
  }

 private:
  [[nodiscard]] static consteval std::size_t index_of(std::string_view field) {
    constexpr auto names = boost::pfr::names_as_array<Type>();
    for (std::size_t at = 0; at != size; ++at) {
      if (names[at] == field) return at;
    }
    throw "knot: the type has no member of that name";
  }

  std::array<std::string_view, size> keys_{};
  std::string_view tag_{};
  std::size_t rest_ = size;
};

// A type that is its one member in JSON: a value given a type of its own --
// a setting, a name -- read and written as what it holds, so that typing it
// changes nothing in the JSON. It says so with a member type named
// json_transparent, which needs nothing of knot to write.
template <class Type>
concept transparent = std::is_aggregate_v<Type> && requires {
  typename Type::json_transparent;
} && boost::pfr::tuple_size_v<Type> == 1;
template <transparent Type>
using transparent_t =
    std::remove_cvref_t<decltype(boost::pfr::get<0>(std::declval<Type &>()))>;

// A type that says what it is called in JSON.
template <class Type>
concept described = std::is_aggregate_v<Type> && requires {
  { json_schema(type<Type>{}) } -> std::same_as<schema<Type>>;
};

template <described Type>
inline constexpr schema<Type> schema_of = json_schema(type<Type>{});

}  // namespace knot
