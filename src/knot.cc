// SPDX-License-Identifier: AGPL-3.0-only
// knot: Canonical JSON for Matrix.
export module knot;

export import knot.schema;
export import knot.format;
export import knot.value;
export import knot.read;
export import knot.write;

import std;

export namespace knot {

// A described type as Canonical JSON -- the form Matrix signs and hashes:
// object keys in the order of their bytes, no whitespace, integers only.
// Written as usual, then read back as a knot::value, whose objects keep
// their keys sorted, and written again from it: a value is written as
// Canonical JSON wherever it holds nothing Canonical JSON cannot say. The
// one thing it cannot is a number with a fraction or an exponent, and a type
// that holds one has no canonical form to sign: said, as a read's error is.
template <class Type>
constexpr std::expected<std::string, error> to_canonical_json(const Type& described) {
  auto sorted = try_read<value>(to_json_string(described));
  if (!sorted)
    return std::unexpected(sorted.error());
  std::string out = to_json_string(*sorted);
  // What has no canonical form, refused: read back as Canonical JSON alone.
  if (auto checked = try_read<value>(out, canonical); !checked)
    return std::unexpected(checked.error());
  return out;
}

}  // namespace knot
