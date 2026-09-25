// knot::by against the plain way: the content always read into a tree, then
// made the type its tag names. Text in memory, many times over; nanoseconds
// and allocations an event.
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

namespace shapes {

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
};
consteval auto json_schema(knot::type<member>) {
  return knot::schema<member>().tag("m.room.member");
}

// The way this library reads it.
struct room_event {
  std::string type;
  knot::by<"type", message, member, knot::value> content;
  std::string event_id;
};
consteval auto json_schema(knot::type<room_event>) {
  return knot::schema<room_event>();
}

// The plain way: a tree, then a type.
struct raw_event {
  std::string type;
  knot::value content;
  std::string event_id;
};
consteval auto json_schema(knot::type<raw_event>) {
  return knot::schema<raw_event>();
}

}  // namespace shapes

namespace {

using content = std::variant<shapes::message, shapes::member, knot::value>;

content plain(shapes::raw_event& event) {
  if (event.type == "m.room.message") {
    if (auto made = knot::from_value<shapes::message>(event.content)) return std::move(*made);
  } else if (event.type == "m.room.member") {
    if (auto made = knot::from_value<shapes::member>(event.content)) return std::move(*made);
  }
  return std::move(event.content);
}

template <class Read>
std::pair<double, double> measure(Read read, int times) {
  for (int at = 0; at != times / 10; ++at) read();  // warm
  const std::size_t before = allocations;
  const auto start = std::chrono::steady_clock::now();
  for (int at = 0; at != times; ++at) read();
  const auto spent = std::chrono::steady_clock::now() - start;
  return {std::chrono::duration<double, std::nano>(spent).count() / times,
          double(allocations - before) / times};
}

}  // namespace

int main() {
  const std::string long_body(2000, 'x');
  std::string cyrillic;
  for (int at = 0; at != 200; ++at) cyrillic += "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 ";
  const std::vector<std::pair<std::string, std::string>> cases{
      {"message, type first",
       R"({"type":"m.room.message","content":{"msgtype":"m.text","body":"hello world, this is a message"},"event_id":"$abcdef"})"},
      {"message, type last",
       R"({"content":{"body":"hello world, this is a message","msgtype":"m.text"},"event_id":"$abcdef","type":"m.room.message"})"},
      {"member, type last",
       R"({"content":{"displayname":"Somebody Long Enough","membership":"join"},"event_id":"$abcdef","type":"m.room.member"})"},
      {"unknown type, last",
       R"({"content":{"body":"hello world, this is a message","msgtype":"m.text"},"event_id":"$abcdef","type":"org.example.note"})"},
      {"redacted, type last",
       R"({"content":{},"event_id":"$abcdef","type":"m.room.message"})"},
      {"long body, type last",
       R"({"content":{"body":")" + long_body + R"(","msgtype":"m.text"},"event_id":"$abcdef","type":"m.room.message"})"},
      {"cyrillic body, type last",
       R"({"content":{"body":")" + cyrillic + R"(","msgtype":"m.text"},"event_id":"$abcdef","type":"m.room.message"})"},
      {"pretty-printed, type last",
       "{\n    \"content\": {\n        \"body\": \"hello world, this is a message\",\n"
       "        \"msgtype\": \"m.text\"\n    },\n    \"event_id\": \"$abcdef\",\n"
       "    \"type\": \"m.room.message\"\n}\n"},
      {"nested unknown, type last",
       R"({"content":{"body":"hi","msgtype":"m.text","m.relates_to":{"rel_type":"m.thread","event_id":"$x","m.in_reply_to":{"event_id":"$y"}},"extra":[1,2,3,{"a":[true,false,null]}]},"event_id":"$abcdef","type":"m.room.message"})"},
  };
  constexpr int times = 100000;
  std::println("{:<28} {:>14} {:>10} {:>14} {:>10}", "case", "by ns", "by allocs",
               "tree ns", "tree allocs");
  for (const auto& [name, text] : cases) {
    std::size_t index_by = 0;
    std::size_t index_tree = 0;
    const auto by = measure([&] {
      auto got = knot::read<shapes::room_event>(text);
      if (!got) std::abort();
      index_by = got->content.data().index();
    }, times);
    const auto tree = measure([&] {
      auto got = knot::read<shapes::raw_event>(text);
      if (!got) std::abort();
      index_tree = plain(*got).index();
    }, times);
    if (index_by != index_tree) {
      std::println("{}: the two ways disagree ({} and {})", name, index_by, index_tree);
    }
    std::println("{:<28} {:>14.0f} {:>10.1f} {:>14.0f} {:>10.1f}", name, by.first,
                 by.second, tree.first, tree.second);
  }
}
