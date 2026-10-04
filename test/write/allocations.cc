import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"
#include "write_shapes.h"

namespace {
thread_local bool counting = false;
thread_local std::size_t allocations = 0;

struct measure {
  measure() { allocations = 0; counting = true; }
  ~measure() { counting = false; }
};
}  // namespace

void* operator new(std::size_t size) {
  if (counting) ++allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
  if (counting) ++allocations;
  const auto align = static_cast<std::size_t>(alignment);
  const auto bytes = ((size ? size : 1) + align - 1) / align * align;
  if (void* p = std::aligned_alloc(align, bytes)) return p;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size, std::align_val_t align) {
  return ::operator new(size, align);
}
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

TEST(WriterAllocations, ShallowLazyTraversalDoesNotAllocatePerElement) {
  const std::vector<event> values(1000, event{std::string(80, 'x'), 2, {"a", "b"}});
  const auto expected = knot::to_json_string(values).size();
  std::size_t bytes = 0;
  {
    measure scope;
    auto view = knot::to_json(values);
    for (auto piece : view.chunks()) bytes += piece.size();
  }
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(bytes, expected);
}

TEST(WriterAllocations, OwningASmallValueNeedsNoSeparateAllocation) {
  std::size_t bytes = 0;
  {
    measure scope;
    auto view = knot::to_json(event{"small", 1, {}});
    for (auto piece : view.chunks()) bytes += piece.size();
  }
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(bytes, std::string_view(R"({"content":"small","depth":1,"prev":[]})").size());
}

TEST(WriterAllocations, DeepTraversalReusesStorageAcrossSiblings) {
  knot::value branch("leaf");
  for (int i = 0; i != 64; ++i) branch = knot::value::array{std::move(branch)};
  const knot::value tree(knot::value::array(100, branch));
  const auto expected = knot::to_json_string(tree).size();
  std::size_t bytes = 0;
  {
    measure scope;
    auto view = knot::to_json(tree);
    for (auto piece : view.chunks()) bytes += piece.size();
  }
  // Allocations grow with maximum depth, not the thousands of containers.
  EXPECT_LT(allocations, 16u);
  EXPECT_EQ(bytes, expected);

  std::string output(expected, '\0');
  char* end = nullptr;
  {
    measure scope;
    end = knot::write(output.data(), tree);
  }
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(end, output.data() + output.size());
  EXPECT_EQ(output, knot::to_json_string(tree));
}

TEST(WriterAllocations, PushingChunksNeedsNoTraversalAllocation) {
  const std::vector<event> values(1000, event{"small", 1, {"a"}});
  const auto expected = knot::to_json_string(values).size();
  std::size_t bytes = 0;
  {
    measure scope;
    knot::write_chunks([&](std::string_view piece) { bytes += piece.size(); }, values);
  }
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(bytes, expected);
}

TEST(WriterAllocations, PassingOverUnknownObjectKeysDoesNotAllocate) {
  const std::string text = "{\"ignored\":{\"" + std::string(1000, 'k') +
                           "\":\"" + std::string(1000, 'v') + "\"}}";
  bool valid = false;
  {
    measure scope;
    valid = knot::try_read<shapes::nothing>(text).has_value();
  }
  EXPECT_TRUE(valid);
  EXPECT_EQ(allocations, 0u);
}
