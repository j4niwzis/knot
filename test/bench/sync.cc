// A /sync response, the shape a client reads most: rooms, their timelines,
// events of several types, most of them with keys nobody here describes.
// Read typed with knot::tagged, and read whole as a knot::value; megabytes a
// second and allocations an event.
import std;
import knot;

namespace {

std::size_t allocations = 0;

}  // namespace

void* operator new(std::size_t size) {
  ++allocations;
  if (void* made = std::malloc(size ? size : 1)) return made;
  throw std::bad_alloc();
}
void operator delete(void* made) noexcept { std::free(made); }
void operator delete(void* made, std::size_t) noexcept { std::free(made); }

namespace matrix_sync {

struct message {
  std::string msgtype;
  std::string body;
};
consteval auto json_schema(knot::type<message>) {
  return knot::schema<message>().tag("m.room.message");
}

struct member {
  std::string membership;
  std::optional<std::string> displayname;
  std::optional<std::string> avatar_url;
};
consteval auto json_schema(knot::type<member>) {
  return knot::schema<member>().tag("m.room.member");
}

struct reaction_key {
  std::string rel_type;
  std::string event_id;
  std::string key;
};
consteval auto json_schema(knot::type<reaction_key>) {
  return knot::schema<reaction_key>();
}
struct reaction {
  reaction_key relates_to;
};
consteval auto json_schema(knot::type<reaction>) {
  return knot::schema<reaction>()
      .member<"relates_to">(knot::key("m.relates_to"))
      .tag("m.reaction");
}

struct unsigned_data {
  std::optional<std::int64_t> age;
};
consteval auto json_schema(knot::type<unsigned_data>) {
  return knot::schema<unsigned_data>();
}

struct event {
  std::string type;
  std::string event_id;
  std::string sender;
  std::int64_t origin_server_ts = 0;
  std::optional<unsigned_data> unsigned_;
  knot::tagged<"type", message, member, reaction, knot::value> content;
};
consteval auto json_schema(knot::type<event>) {
  return knot::schema<event>().member<"unsigned_">(knot::key("unsigned"));
}

struct timeline {
  std::vector<event> events;
  bool limited = false;
  std::optional<std::string> prev_batch;
};
consteval auto json_schema(knot::type<timeline>) {
  return knot::schema<timeline>();
}

struct joined_room {
  matrix_sync::timeline timeline;
};
consteval auto json_schema(knot::type<joined_room>) {
  return knot::schema<joined_room>();
}

struct rooms {
  std::flat_map<std::string, joined_room> join;
};
consteval auto json_schema(knot::type<rooms>) {
  return knot::schema<rooms>();
}

struct response {
  std::string next_batch;
  matrix_sync::rooms rooms;
};
consteval auto json_schema(knot::type<response>) {
  return knot::schema<response>();
}

// A response, written the way a server writes one: type last, as Synapse
// often does, keys it has that we do not describe.
std::string made_up(int rooms, int per_room) {
  std::string out = R"({"next_batch":"s72595_4483_1934","rooms":{"join":{)";
  for (int room = 0; room != rooms; ++room) {
    if (room) out += ',';
    out += std::format(R"("!room{}:example.org":{{"timeline":{{"events":[)", room);
    for (int at = 0; at != per_room; ++at) {
      if (at) out += ',';
      const std::string head = std::format(
          R"("event_id":"$event{}_{}:example.org","origin_server_ts":{},"sender":"@user{}:example.org","unsigned":{{"age":{}}})",
          room, at, 1432735824653 + at, at % 7, 1234 + at);
      switch (at % 5) {
        case 0:
        case 1:
          out += std::format(
              R"({{"content":{{"body":"This is message number {} in a room, with some text in it","msgtype":"m.text"}},{},"type":"m.room.message"}})",
              at, head);
          break;
        case 2:
          out += std::format(
              R"({{"content":{{"avatar_url":"mxc://example.org/SEsfnsuifSDFSSEF","displayname":"User {}","membership":"join"}},{},"state_key":"@user{}:example.org","type":"m.room.member"}})",
              at, head, at);
          break;
        case 3:
          out += std::format(
              R"({{"content":{{"m.relates_to":{{"event_id":"$event{}_0:example.org","key":"👍","rel_type":"m.annotation"}}}},{},"type":"m.reaction"}})",
              room, head);
          break;
        default:
          out += std::format(
              R"({{"content":{{"body":"Привет, это сообщение с ответом","format":"org.matrix.custom.html","formatted_body":"<mx-reply><blockquote>quoted</blockquote></mx-reply>Привет","m.relates_to":{{"m.in_reply_to":{{"event_id":"$event{}_0:example.org"}}}},"msgtype":"m.text"}},{},"type":"m.room.message"}})",
              room, head);
      }
    }
    out += R"(],"limited":false,"prev_batch":"t34-23535_0_0"}})";
  }
  out += "}}}";
  return out;
}

struct whole {
  std::string next_batch;
  knot::value rooms;
};
consteval auto json_schema(knot::type<whole>) { return knot::schema<whole>(); }

}  // namespace matrix_sync

int main() {
  const std::string text = matrix_sync::made_up(20, 50);
  const double megabytes = double(text.size()) / 1e6;
  constexpr int rounds = 30;
  const auto measure = [&](auto read) {
    read();
    double best = 1e300;
    std::size_t allocated = 0;
    for (int round = 0; round != rounds; ++round) {
      const std::size_t before = allocations;
      const auto start = std::chrono::steady_clock::now();
      read();
      best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
      allocated = allocations - before;
    }
    return std::pair{best, allocated};
  };
  std::size_t typed = 0;
  const auto by = measure([&] {
    auto got = knot::try_read<matrix_sync::response>(text);
    if (!got) {
      std::println("{} at {}", got.error().message, got.error().offset);
      std::abort();
    }
    typed = 0;
    for (const auto& [id, room] : got->rooms.join) {
      for (const auto& one : room.timeline.events) {
        typed += !one.content.is<knot::value>();
      }
    }
  });
  // The same response as Canonical JSON, written by knot, read back strictly.
  const std::string canonical = [&] {
    auto got = knot::try_read<matrix_sync::response>(text);
    return std::ranges::to<std::string>(knot::to_json(*got));
  }();
  const auto strict = measure([&] {
    auto got = knot::try_read<matrix_sync::response>(canonical, knot::canonical);
    if (!got) {
      std::println("{} at {}", got.error().message, got.error().offset);
      std::abort();
    }
  });
  // Writing it: the lazy view, gathered into a string.
  const auto typed_response = *knot::try_read<matrix_sync::response>(text);
  const auto written = measure([&] {
    auto out = std::ranges::to<std::string>(knot::to_json(typed_response));
    if (out.size() != canonical.size()) std::abort();
  });
  const auto eager = measure([&] {
    const std::string out = knot::to_json_string(typed_response);
    if (out.size() != canonical.size()) std::abort();
  });
  const auto tree = measure([&] {
    auto got = knot::try_read<matrix_sync::whole>(text);
    if (!got) std::abort();
  });
  std::println("a /sync of {:.2f} MB, 1000 events, {} of them typed", megabytes, typed);
  std::println("{:<22} {:>10} {:>14}", "", "MB/s", "allocs/event");
  std::println("{:<22} {:>10.0f} {:>14.1f}", "knot::tagged, typed", megabytes / by.first,
               double(by.second) / 1000);
  std::println("{:<22} {:>10.0f} {:>14.1f}", "knot::tagged, canonical",
               double(canonical.size()) / 1e6 / strict.first, double(strict.second) / 1000);
  std::println("{:<22} {:>10.0f} {:>14.1f}", "to_json_string",
               double(canonical.size()) / 1e6 / eager.first, double(eager.second) / 1000);
  std::println("{:<22} {:>10.0f} {:>14.1f}", "to_json, written",
               double(canonical.size()) / 1e6 / written.first, double(written.second) / 1000);
  std::println("{:<22} {:>10.0f} {:>14.1f}", "knot::value, whole", megabytes / tree.first,
               double(tree.second) / 1000);
}
