# knot

JSON for Matrix, as C++23 modules: ordinary JSON and Canonical JSON read
straight into your types, and your types written as Canonical JSON.

```cpp
import std;
import knot;

struct message {
  std::string msgtype;
  std::string body;
};
consteval auto json_schema(knot::type<message>) {
  return knot::schema<message>().tag("m.room.message");
}

struct room_event {
  std::string type;
  knot::by<"type", message, knot::value> content;
  std::string event_id;
  std::optional<std::int64_t> origin_server_ts;
};
consteval auto json_schema(knot::type<room_event>) {
  return knot::schema<room_event>();
}

auto event = knot::read<room_event>(text);                    // any JSON
auto signed_event = knot::read<room_event>(text, knot::canonical);
std::string out = knot::to_json(*event) | std::ranges::to<std::string>();
```

No document object model is built on the way, and no automaton either: for a
type known in advance JSON is decided by the next character everywhere, so the
reader is written from the type -- a function a type, the nesting the type's
own -- and reads any input range once, looking one character ahead.

## Describing a type

A type opts in with a function found by argument-dependent lookup, the way
chevron's types do. Its members are its keys, named as the members are:

```cpp
consteval auto json_schema(knot::type<content>) {
  return knot::schema<content>()
      .member<"relates_to">(knot::key("m.relates_to"))  // a key that is no C++ name
      .tag("org.example.content");                      // for knot::by, below
}
```

| member type | JSON |
| --- | --- |
| `std::string` | a string, UTF-8 checked |
| `bool` | `true`, `false` |
| any integer type | a whole number within ±(2^53 − 1) that fits the member |
| `std::optional<T>` | the key may be absent (or null) |
| `std::vector<T>` | an array |
| `std::map<std::string, T>`, `std::flat_map<std::string, T>` | an object whose keys are the data |
| a described type | an object |
| `knot::value` | anything |
| `knot::by<"key", A, B, …>` | one of several, chosen by a sibling key |

## Reading

```cpp
std::expected<T, knot::error> knot::read<T>(text);                   // any JSON (RFC 8259)
std::expected<T, knot::error> knot::read<T>(text, knot::canonical);  // Canonical JSON only
```

`text` is a `std::string_view` or any input range of `char`: a
`std::list<char>`, pieces `| std::views::join`, a stream through
`std::istreambuf_iterator`. `knot::error` says what and how many characters
in.

**Ordinary JSON** may have white space anywhere, keys in any order, any escape
(surrogate pairs taken together), and a number in any spelling as long as it
is a whole one in range: `1.0`, `1e3` and `10e-1` are numbers, `1.5` is not.

**Canonical JSON** is refused unless it is exactly that: no white space, keys
sorted by their bytes, only the escapes it must have, integers as digits. Keys
are merged against the type's own sorted keys, one comparison a key.

In both, a key the type does not have is passed over, a key twice is refused,
text that is not UTF-8 is refused, and nesting in what is passed over stops at
128.

## Content chosen by a key: `knot::by`

```cpp
knot::by<"type", message, member, knot::value> content;
content.is<message>();  content.as<message>();  content.data();  // a std::variant
content.unknown;        // what the content had that the alternative does not
```

Each alternative says its tag in its schema; `knot::value` last takes what no
tag names, and what a named alternative does not fit (a redacted event, say).
Only one value is ever made, whichever order `"type"` and the content come in:

- `"type"` first: the content is read into the alternative it names;
- `"type"` last -- always so in Canonical JSON: the content is read into the
  alternative its first key points to, keys it does not have kept beside it;
- at the first thing that does not fit, that level turns what it has into a
  tree, moving it, and reads the rest into the tree;
- once `"type"` is read, the content becomes the alternative it names --
  moved across member by member where it was read into another, from the tree
  where it turned -- or stays a tree where the named one does not fit.

Written again, a `knot::by` writes its kept keys back: an event read and
written loses nothing.

## `knot::value`

Any JSON: null, bool, `std::int64_t`, `double` (ordinary JSON only), string,
`std::vector<value>`, `std::flat_map<std::string, value>`. `v["key"]`,
`v.is<T>()`, `v.as<T>()`, `v.data()`. `knot::from_value<T>(tree)` makes a type
of it by moving, and keeps the tree whole where it does not fit.

## Writing

```cpp
knot::to_json(value) | std::ranges::to<std::string>();
for (std::string_view piece : knot::to_json(value).chunks()) send(piece);
```

A lazy view of the document's characters, made as they are pulled, in
Canonical JSON: keys sorted, only the escapes it must have, absent optionals
left out. The pieces point into the value where they can.

Reading and writing are both `constexpr`: a document is written and read back
inside a `static_assert` in the tests.

## Speed

Where the text is in memory, strings are taken a run at a time: the end of a
run is found 32 bytes at a time, ASCII passed 32 bytes at a time, and the rest
checked as UTF-8 16 bytes at a time with simdjson's lookup method (SSSE3,
chosen at run time); white space is skipped the same way.

`test/bench/by.cc`, one core, best of five rounds, nanoseconds an event and
allocations -- `knot::by` against reading the content into a tree and then
typing it:

| case | knot::by | allocs | tree, then type | allocs |
| --- | ---: | ---: | ---: | ---: |
| message, type first | 163 | 1 | 274 | 8 |
| message, type last | 171 | 1 | 298 | 8 |
| member, type last | 152 | 0 | 296 | 7 |
| unknown type | 262 | 5 | 217 | 8 |
| body of 2000 ASCII bytes | 229 | 1 | 377 | 8 |
| body of 2400 bytes of Cyrillic | 398 | 1 | 704 | 8 |
| pretty-printed | 181 | 1 | 329 | 8 |

## Building

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Clang with `import std`; Boost.PFR as a module, through cmake-everywhere. The
tests are one executable a case: `test/format` (the generator of scan formats,
alone), `test/read`, `test/write`, and `test/scan` -- scan alone, on formats of
the kind knot.format writes -- where `KNOT_SCAN_DIR` names a checkout of scan.
