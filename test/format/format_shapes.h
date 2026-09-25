// Shared by the tests beside it; included after the imports.
#pragma once

// The generator alone: what knot.format writes for a type, compared as text.
// Nothing here imports scan.


namespace shapes {

struct event {
  std::string content;
  std::int64_t depth;
  std::vector<std::string> prev;
};
consteval auto json_schema(knot::type<event>) { return knot::schema<event>(); }

struct unsorted {
  int zebra;
  bool apple;
  std::string mango;
};
consteval auto json_schema(knot::type<unsorted>) {
  return knot::schema<unsorted>();
}

struct relates {
  std::string event_id;
  std::string rel_type;
};
consteval auto json_schema(knot::type<relates>) {
  return knot::schema<relates>();
}

struct content {
  relates relates_to;
  std::string body;
};
consteval auto json_schema(knot::type<content>) {
  return knot::schema<content>().member<"relates_to">(
      knot::key("m.relates_to"));
}

struct awkward {
  int plain;
  int odd;
};
consteval auto json_schema(knot::type<awkward>) {
  return knot::schema<awkward>().member<"odd">(knot::key("a{b}\"c\n"));
}

struct nothing {};
consteval auto json_schema(knot::type<nothing>) {
  return knot::schema<nothing>();
}

struct counts {
  std::uint32_t seen;
  std::vector<std::vector<std::int64_t>> grid;
};
consteval auto json_schema(knot::type<counts>) {
  return knot::schema<counts>();
}

}  // namespace shapes

namespace {

using namespace std::string_literals;
using namespace std::string_view_literals;

const std::string string{knot::patterns::string};
const std::string integer{knot::patterns::integer};

// Said at compile time too, which is when it is used.
static_assert(knot::scan_format<shapes::event>.view() ==
              R"(\{"content":{},"depth":{},"prev":{}\})"sv);

}  // namespace
