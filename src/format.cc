// knot.format: a described type's Canonical JSON, said as text for scan.
//
// Two things come out of a type, both while the program is compiled and both
// plain text, so that they are checked without scan being there at all:
//
//   scan_format<T>  the format scan reads T by -- the keys in the order
//                   Canonical JSON sorts them, each value a place:
//                   \{"content":{},"depth":{}\}
//   pattern<T>      the whole of T's JSON as one pattern with no groups, for
//                   where a value is read inside something else (an element
//                   of an array)
//
// And the patterns of the leaves: a string exactly as Canonical JSON writes it
// (valid UTF-8, only the escapes it must have), an integer in the range it
// allows, true and false.
//
// Canonical JSON is regular for a type known in advance: no whitespace, one
// order of keys, one spelling of every value. Nesting is bounded by the type.
export module knot.format;

import std;
import boost.pfr;
export import knot.schema;

export namespace knot {

// Text of a size known while compiling, with a terminator after it.
template <std::size_t Size>
struct text {
  char value[Size + 1]{};
  [[nodiscard]] constexpr std::string_view view() const {
    return {value, Size};
  }
  [[nodiscard]] static constexpr std::size_t size() { return Size; }
};

namespace patterns {

// A string as Canonical JSON writes it. Printable ASCII but '"' and '\\' as
// itself; those two and the five controls with a short form escaped that way;
// every other control as \u00xx in lower case; anything else as well-formed
// UTF-8 -- no surrogates, nothing overlong, nothing past U+10FFFF.
inline constexpr std::string_view string =
    R"("(?:[\x20\x21\x23-\x5b\x5d-\x7f])"
    R"(|\\["\\bfnrt])"
    R"(|\\u00(?:0[0-7bef]|1[0-9a-f]))"
    R"(|[\xc2-\xdf][\x80-\xbf])"
    R"(|\xe0[\xa0-\xbf][\x80-\xbf])"
    R"(|[\xe1-\xec\xee\xef][\x80-\xbf]{2})"
    R"(|\xed[\x80-\x9f][\x80-\xbf])"
    R"(|\xf0[\x90-\xbf][\x80-\xbf]{2})"
    R"(|[\xf1-\xf3][\x80-\xbf]{3})"
    R"(|\xf4[\x80-\x8f][\x80-\xbf]{2})*")";

// An integer: no leading zeros, no "-0", no fraction, no exponent, and at
// most sixteen digits -- the range check itself, |n| < 2^53, is the reader's.
inline constexpr std::string_view integer = "(?:0|-?[1-9][0-9]{0,15})";
// The same for a field that cannot be negative.
inline constexpr std::string_view natural = "(?:0|[1-9][0-9]{0,15})";

inline constexpr std::string_view boolean = "(?:true|false)";

}  // namespace patterns

namespace detail {

template <class Type>
concept character =
    std::same_as<Type, char> || std::same_as<Type, signed char> ||
    std::same_as<Type, unsigned char> || std::same_as<Type, wchar_t> ||
    std::same_as<Type, char8_t> || std::same_as<Type, char16_t> ||
    std::same_as<Type, char32_t>;

template <class Type>
concept json_integer =
    std::integral<Type> && !std::same_as<Type, bool> && !character<Type>;

template <class Type>
struct is_vector : std::false_type {};
template <class Element, class Allocator>
struct is_vector<std::vector<Element, Allocator>> : std::true_type {};

// An object with keys of its own: a std::map from strings, which keeps them
// in the order Canonical JSON writes them.
template <class Type>
struct is_map : std::false_type {};
template <class Value, class Allocator>
struct is_map<std::map<std::string, Value, std::less<std::string>, Allocator>>
    : std::true_type {};
template <class Value, class Allocator>
struct is_map<std::map<std::string, Value, std::less<>, Allocator>>
    : std::true_type {};

// The same in sorted vectors: std::flat_map.
template <class Type>
struct is_flat_map : std::false_type {};
template <class Value, class Keys, class Values>
struct is_flat_map<std::flat_map<std::string, Value, std::less<std::string>, Keys, Values>>
    : std::true_type {};
template <class Value, class Keys, class Values>
struct is_flat_map<std::flat_map<std::string, Value, std::less<>, Keys, Values>>
    : std::true_type {};
template <class Value, class Keys, class Values>
struct is_map<std::flat_map<std::string, Value, std::less<std::string>, Keys, Values>>
    : std::true_type {};
template <class Value, class Keys, class Values>
struct is_map<std::flat_map<std::string, Value, std::less<>, Keys, Values>>
    : std::true_type {};

template <class Type>
struct is_optional : std::false_type {};
template <class Value>
struct is_optional<std::optional<Value>> : std::true_type {};

// A string that is one of a few: a std::variant of empty types, each saying
// the string it is --
//   struct read { static constexpr std::string_view json_value = "m.read"; };
// -- and last, perhaps, a std::string for any string the others are not.
template <class Type>
concept named_string =
    std::is_class_v<Type> && std::is_empty_v<Type> &&
    std::default_initializable<Type> && requires {
      { Type::json_value } -> std::convertible_to<std::string_view>;
    };

template <class... Alternatives>
consteval bool choice_shape() {
  constexpr std::size_t count = sizeof...(Alternatives);
  if constexpr (count == 0) {
    return false;
  } else {
    constexpr bool open = std::same_as<Alternatives...[count - 1], std::string>;
    constexpr std::size_t named = count - (open ? 1 : 0);
    if constexpr (named == 0) {
      return false;
    } else {
      return []<std::size_t... At>(std::index_sequence<At...>) {
        return (named_string<Alternatives...[At]> && ...);
      }(std::make_index_sequence<named>{});
    }
  }
}

template <class Type>
struct is_choice : std::false_type {};
template <class... Alternatives>
struct is_choice<std::variant<Alternatives...>>
    : std::bool_constant<choice_shape<Alternatives...>()> {};

template <class Type>
struct choice;

template <class... Alternatives>
struct choice<std::variant<Alternatives...>> {
  using type = std::variant<Alternatives...>;
  static constexpr std::size_t count = sizeof...(Alternatives);
  // Whether a string none of the others is, is kept as it is.
  static constexpr bool open = std::same_as<Alternatives...[count - 1], std::string>;
  static constexpr std::size_t named = count - (open ? 1 : 0);

  static constexpr auto names = []<std::size_t... At>(std::index_sequence<At...>) {
    return std::array<std::string_view, named>{
        std::string_view(Alternatives...[At]::json_value)...};
  }(std::make_index_sequence<named>{});

  static consteval bool distinct() {
    for (std::size_t one = 0; one < named; ++one) {
      for (std::size_t other = one + 1; other < named; ++other) {
        if (names[one] == names[other]) return false;
      }
    }
    return true;
  }
  static_assert(distinct(), "knot: two alternatives of a choice say one string");

  // The alternative a string names, or count where none does.
  static constexpr std::size_t find(std::string_view text) {
    for (std::size_t at = 0; at < named; ++at) {
      if (names[at] == text) return at;
    }
    return count;
  }

  // The string an alternative is: for the kept one, what was kept.
  static constexpr std::string_view name(const type& held) {
    if constexpr (open) {
      if (held.index() == count - 1) return std::get<count - 1>(held);
    }
    return names[held.index()];
  }

  // The alternative a string names made, or the string kept if the choice is
  // open; false, with nothing moved, where neither.
  static constexpr bool settle(std::string& text, type& out) {
    const std::size_t at = find(text);
    if (at == count) {
      if constexpr (open) {
        out.template emplace<count - 1>(std::move(text));
        return true;
      } else {
        return false;
      }
    }
    [&]<std::size_t... At>(std::index_sequence<At...>) {
      (void)((at == At ? (out.template emplace<At>(), true) : false) || ...);
    }(std::make_index_sequence<named>{});
    return true;
  }
};

template <class Type, std::size_t Index>
using field_t = std::remove_cvref_t<boost::pfr::tuple_element_t<Index, Type>>;

}  // namespace detail

// The members of a type in the order their keys are written: sorted by the
// bytes of the key, which for UTF-8 is the order of code points.
template <described Type>
consteval auto sorted_order() {
  constexpr std::size_t size = schema<Type>::size;
  std::array<std::size_t, size> order{};
  for (std::size_t at = 0; at != size; ++at) order[at] = at;
  std::ranges::sort(order, [](std::size_t left, std::size_t right) {
    return schema_of<Type>.key_of(left) < schema_of<Type>.key_of(right);
  });
  for (std::size_t at = 1; at < size; ++at) {
    if (schema_of<Type>.key_of(order[at - 1]) ==
        schema_of<Type>.key_of(order[at])) {
      throw "knot: two members with one key";
    }
  }
  return order;
}

template <described Type>
inline constexpr auto order_of = sorted_order<Type>();

namespace detail {

// A character of the text as itself, whichever it is: scan's formats and
// patterns both take a backslash before one to mean that character.
constexpr void append_literal(std::string& out, char letter) {
  if (std::string_view(".^$|()[]*+?{}\\").contains(letter)) out += '\\';
  out += letter;
}

constexpr void append_hex(std::string& out, unsigned value) {
  out += "0123456789abcdef"[value];
}

// A string as Canonical JSON writes it, quotes and all.
constexpr void append_json_string(std::string& json, std::string_view key) {
  json += '"';
  for (const char letter : key) {
    const auto byte = static_cast<unsigned char>(letter);
    switch (letter) {
      case '"': json += "\\\""; break;
      case '\\': json += "\\\\"; break;
      case '\b': json += "\\b"; break;
      case '\f': json += "\\f"; break;
      case '\n': json += "\\n"; break;
      case '\r': json += "\\r"; break;
      case '\t': json += "\\t"; break;
      default:
        if (byte < 0x20) {
          json += "\\u00";
          append_hex(json, byte >> 4);
          append_hex(json, byte & 0xf);
        } else {
          json += letter;
        }
    }
  }
  json += '"';
}

// A key as Canonical JSON writes it, and each character of that as itself.
constexpr void append_key(std::string& out, std::string_view key) {
  std::string json;
  append_json_string(json, key);
  for (const char letter : json) append_literal(out, letter);
}

template <class Type>
constexpr void append_pattern(std::string& out);

// An array: none, or one and then more after commas. The element is said
// twice; with groups, each is a group, which is what a fold is told.
template <class Element>
constexpr void append_array(std::string& out, bool groups) {
  std::string element;
  append_pattern<Element>(element);
  const std::string once = groups ? "(" + element + ")" : element;
  out += "\\[(?:" + once + "(?:," + once + ")*)?\\]";
}

// An object: its keys in their order, and for each value either a place or
// the value's own pattern.
template <class Type>
constexpr void append_object(std::string& out, bool places) {
  out += "\\{";
  [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
    (([&] {
       constexpr std::size_t member = order_of<Type>[Rank];
       if (Rank != 0) out += ',';
       append_key(out, schema_of<Type>.key_of(member));
       out += ':';
       if (places) {
         out += "{}";
       } else {
         append_pattern<field_t<Type, member>>(out);
       }
     }()),
     ...);
  }(std::make_index_sequence<schema<Type>::size>{});
  out += "\\}";
}

template <class Type>
constexpr void append_pattern(std::string& out) {
  if constexpr (std::same_as<Type, std::string>) {
    out += patterns::string;
  } else if constexpr (std::same_as<Type, bool>) {
    out += patterns::boolean;
  } else if constexpr (json_integer<Type>) {
    out += std::is_signed_v<Type> ? patterns::integer : patterns::natural;
  } else if constexpr (is_choice<Type>::value) {
    if constexpr (choice<Type>::open) {
      out += patterns::string;
    } else {
      out += "(?:";
      for (std::size_t at = 0; at < choice<Type>::named; ++at) {
        if (at != 0) out += '|';
        append_key(out, choice<Type>::names[at]);
      }
      out += ')';
    }
  } else if constexpr (is_vector<Type>::value) {
    append_array<typename Type::value_type>(out, false);
  } else if constexpr (described<Type>) {
    append_object<Type>(out, false);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

// What a function makes, kept as text of exactly its size.
template <auto Make>
consteval auto freeze() {
  constexpr std::size_t size = Make().size();
  text<size> made;
  const std::string said = Make();
  std::ranges::copy(said, made.value);
  return made;
}

// An array read by a fold: each element a group.
template <class Element>
inline constexpr auto array_groups = freeze<[] {
  std::string out;
  append_array<Element>(out, true);
  return out;
}>();

// The key of a member as it is written, quotes, colon and all: "content":
template <class Type, std::size_t Rank>
inline constexpr auto key_literal = freeze<[] {
  std::string out;
  append_json_string(out, schema_of<Type>.key_of(order_of<Type>[Rank]));
  out += ':';
  return out;
}>();

// Whether the member of each rank has to be there: all but the optional ones,
// and the one the rest of the keys go to.
template <class Type>
inline constexpr auto required_at = []<std::size_t... Rank>(
                                        std::index_sequence<Rank...>) {
  return std::array<bool, sizeof...(Rank)>{
      (!is_optional<field_t<Type, order_of<Type>[Rank]>>::value &&
       order_of<Type>[Rank] != schema_of<Type>.rest_member())...};
}(std::make_index_sequence<schema<Type>::size>{});

// Whether a type keeps the rest of its keys, and where.
template <class Type>
inline constexpr bool keeps_rest = schema_of<Type>.rest_member() != schema<Type>::size;

}  // namespace detail

template <described Type>
inline constexpr auto scan_format = detail::freeze<[] {
  std::string out;
  detail::append_object<Type>(out, true);
  return out;
}>();

template <class Type>
inline constexpr auto pattern = detail::freeze<[] {
  std::string out;
  detail::append_pattern<Type>(out);
  return out;
}>();

}  // namespace knot
