// Shared by the tests beside it; included after the imports.
#pragma once

// Documents read into types, and every way a document can be JSON and still
// not Canonical JSON refused.


namespace shapes {

struct event {
  std::string content;
  std::int64_t depth = 0;
  std::vector<std::string> prev;
};
consteval auto json_schema(knot::type<event>) { return knot::schema<event>(); }

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
  bool edited = false;
};
consteval auto json_schema(knot::type<content>) {
  return knot::schema<content>().member<"relates_to">(
      knot::key("m.relates_to"));
}

struct sizes {
  std::uint32_t count = 0;
  std::int32_t small = 0;
};
consteval auto json_schema(knot::type<sizes>) { return knot::schema<sizes>(); }

struct many {
  std::vector<relates> links;
  std::vector<std::vector<std::int64_t>> grid;
};
consteval auto json_schema(knot::type<many>) { return knot::schema<many>(); }

}  // namespace shapes

namespace {

using shapes::event;

// Canonical JSON and nothing else.
template <class Type>
constexpr auto strict(auto&& text) {
  return knot::read<Type>(std::forward<decltype(text)>(text), knot::canonical);
}

}  // namespace
