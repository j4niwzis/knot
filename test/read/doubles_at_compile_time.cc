// Numbers with fractions read while the compiler evaluates: knot works the
// nearest double out exactly there, and the compiler's own reading of the
// same literal says what it must be -- bit for bit.
import std;
import knot;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

#define KNOT_NUMBERS(X)                                                          \
  X(0.1) X(0.2) X(0.3) X(0.7) X(1.5) X(-2e3) X(0.30000000000000004)              \
  X(3.141592653589793) X(2.718281828459045) X(1e23) X(8.98846567431158e307)      \
  X(1.7976931348623157e308) X(1.7976931348623158e308) X(2.2250738585072011e-308) \
  X(2.2250738585072014e-308) X(4.9406564584124654e-324) X(5e-324)                \
  X(2.4703282292062328e-324) X(1e-320) X(9007199254740993.0)                     \
  X(9007199254740995.0) X(18014398509481985.0) X(123456789012345678901234567890e-10) \
  X(0.000001) X(1e-7) X(123.456e-300) X(6.02214076e23) X(-0.0) X(0.0e10)         \
  X(1.00000000000000011102230246251565404236316680908203125)                     \
  X(1.00000000000000011102230246251565404236316680908203124)                     \
  X(1.00000000000000011102230246251565404236316680908203126)                     \
  X(7.2057594037927933e16) X(1e308) X(1e-300) X(4.35e-12) X(0.5e1) X(25e-1)

// The place of the first number read wrong, where one is; -1 where none is.
consteval int first_wrong() {
  int at = 0;
#define KNOT_ONE(number)                                                               \
  if (std::bit_cast<std::uint64_t>(*knot::try_read<double>(#number)) !=                \
      std::bit_cast<std::uint64_t>(double(number)))                                    \
    return at;                                                                         \
  ++at;
  KNOT_NUMBERS(KNOT_ONE)
#undef KNOT_ONE
  return -1;
}
static_assert(first_wrong() == -1);
consteval bool all_nearest() { return first_wrong() == -1; }

consteval bool too_large() {
  return !knot::try_read<double>("1e309") && !knot::try_read<double>("-2e308") &&
         knot::try_read<double>("1e-400").value() == 0.0;
}
static_assert(too_large());

TEST(Read, DoublesAtCompileTime) {
  EXPECT_FALSE(knot::try_read<double>("1e309"));
  EXPECT_EQ(knot::try_read<double>("1e-400").value(), 0.0);
  // The same at run time, where std::from_chars reads them.
#define KNOT_ONE(number)                                                                 \
  EXPECT_EQ(std::bit_cast<std::uint64_t>(*knot::try_read<double>(#number)),              \
            std::bit_cast<std::uint64_t>(double(number))) << #number;
  KNOT_NUMBERS(KNOT_ONE)
#undef KNOT_ONE
  EXPECT_TRUE(all_nearest());
}

}  // namespace
