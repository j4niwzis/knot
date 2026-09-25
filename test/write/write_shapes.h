// Shared by the tests beside it; included after the imports.
#pragma once

namespace shapes {

struct event {
  std::string content;
  std::int64_t depth = 0;
  std::vector<std::string> prev;
  friend bool operator==(const event&, const event&) = default;
};
consteval auto json_schema(knot::type<event>) { return knot::schema<event>(); }

struct relates {
  std::string event_id;
  std::string rel_type;
  friend bool operator==(const relates&, const relates&) = default;
};
consteval auto json_schema(knot::type<relates>) {
  return knot::schema<relates>();
}

struct content {
  relates relates_to;
  std::string body;
  bool edited = false;
  std::vector<std::vector<std::uint16_t>> grid;
  friend bool operator==(const content&, const content&) = default;
};
consteval auto json_schema(knot::type<content>) {
  return knot::schema<content>().member<"relates_to">(
      knot::key("m.relates_to"));
}

struct nothing {
  friend bool operator==(const nothing&, const nothing&) = default;
};
consteval auto json_schema(knot::type<nothing>) {
  return knot::schema<nothing>();
}

}  // namespace shapes

namespace {

using shapes::event;

constexpr std::string json(const auto& value) {
  return knot::to_json(value) | std::ranges::to<std::string>();
}

}  // namespace
