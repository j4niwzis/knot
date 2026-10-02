import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {

// Written through an output iterator, piece by piece: into bytes here, by
// a transform -- the same characters the eager writer gives.
TEST(Write, ThroughAnOutputIterator) {
  const event value{"through", 2, {"$a", "$b"}};
  std::vector<std::uint8_t> bytes;
  auto into = std::back_inserter(bytes);
  struct as_byte {
    std::back_insert_iterator<std::vector<std::uint8_t>> out;
    using difference_type = std::ptrdiff_t;
    as_byte& operator*() { return *this; }
    as_byte& operator=(char c) {
      *out++ = std::bit_cast<std::uint8_t>(c);
      return *this;
    }
    as_byte& operator++() { return *this; }
    as_byte operator++(int) { return *this; }
  };
  (void)knot::write(as_byte{into}, value);
  const std::string written = bytes | std::views::transform([](std::uint8_t b) { return std::bit_cast<char>(b); }) |
                              std::ranges::to<std::string>();
  EXPECT_EQ(written, knot::to_json_string(value));
}

}  // namespace
