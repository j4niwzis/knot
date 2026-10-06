// SPDX-License-Identifier: AGPL-3.0-only
// knot.write: a described value as its Canonical JSON, lazily.
//
//   std::string text = std::ranges::to<std::string>(knot::to_json(event));
//   for (std::string_view piece : knot::to_json(event).chunks()) send(piece);
//
// The document is made as it is pulled, piece by piece -- a key with what
// surrounds it, a run of a string that needs no escaping, an escape, a number
// -- and nothing of it is held but the piece being read. The pieces point into
// the value where they can, so a long string is handed over in one piece.
//
// All of it is constexpr: a document can be written while the program is
// compiled, the frames freed before the evaluation ends.
//
// What Canonical JSON cannot say is the caller's to keep out: integers beyond
// 2^53 - 1 either way, and strings that are not UTF-8. They are written as they
// are, and knot::read refuses them.
export module knot.write;

import std;
import splice;
import boost.pfr;
export import knot.format;
export import knot.value;
import knot.read;

namespace knot::detail {

// Whether a rest member holds no keys: then the object is written from its
// members alone.
constexpr bool rest_empty(const value& kept) {
  return kept.is_null() || (kept.is<value::object>() && kept.as<value::object>().empty());
}
constexpr bool rest_empty(const raw& kept) { return kept.text.empty() || kept.text == "{}"; }

}  // namespace knot::detail

namespace knot::detail::lazy {

// Leaves are described without a frame. Container frames keep their concrete
// types in the machine's variant; entering a child never allocates a frame.
struct step {
  enum class what { piece, child, done, string, integer, natural, floating } kind = what::done;
  std::string_view text;
  std::int64_t integer = 0;
  double floating = 0;
  std::uint64_t natural = 0;

  static constexpr step piece(std::string_view text) { return {what::piece, text}; }
  static constexpr step into() { return {what::child, {}}; }
  static constexpr step done() { return {}; }
  static constexpr step string(std::string_view text) { return {what::string, text}; }
  static constexpr step whole(std::int64_t number) { return {what::integer, {}, number}; }
  static constexpr step whole(std::uint64_t number) { return {what::natural, {}, 0, 0, number}; }
  static constexpr step fraction(double number) { return {what::floating, {}, 0, number}; }
};

template <class Type, class Machine>
constexpr step child_step(const Type& value, Machine& machine);

// A string: its quotes, runs that need nothing, and the escapes between them.
class string_frame {
 public:
  constexpr explicit string_frame(std::string_view text) : text_(text) {}

  constexpr step next() {
    if (!opened_) {
      opened_ = true;
      return step::piece("\"");
    }
    if (at_ == text_.size()) {
      if (closed_) return step::done();
      closed_ = true;
      return step::piece("\"");
    }
    const std::size_t from = at_;
    while (at_ != text_.size() && plain(text_[at_])) ++at_;
    if (at_ != from) return step::piece(text_.substr(from, at_ - from));
    return step::piece(escape(text_[at_++]));
  }

 private:
  static constexpr bool plain(char letter) {
    const auto byte = static_cast<unsigned char>(letter);
    return byte >= 0x20 && letter != '"' && letter != '\\';
  }

  constexpr std::string_view escape(char letter) {
    switch (letter) {
      case '"': return "\\\"";
      case '\\': return "\\\\";
      case '\b': return "\\b";
      case '\f': return "\\f";
      case '\n': return "\\n";
      case '\r': return "\\r";
      case '\t': return "\\t";
      default: {
        constexpr std::string_view digits = "0123456789abcdef";
        const auto byte = static_cast<unsigned char>(letter);
        escaped_ = {'\\', 'u', '0', '0', digits[byte >> 4], digits[byte & 0xf]};
        return {escaped_.data(), escaped_.size()};
      }
    }
  }

  std::string_view text_;
  std::size_t at_ = 0;
  std::array<char, 6> escaped_{};
  bool opened_ = false;
  bool closed_ = false;
};

template <class Element, class Allocator>
class array_frame {
 public:
  constexpr explicit array_frame(const std::vector<Element, Allocator>& values)
      : values_(values) {}

  template <class Machine>
  constexpr step next(Machine& machine) {
    if (!opened_) {
      opened_ = true;
      return step::piece("[");
    }
    if (at_ == values_.size()) {
      if (closed_) return step::done();
      closed_ = true;
      return step::piece("]");
    }
    if (at_ != 0 && !comma_) {
      comma_ = true;
      return step::piece(",");
    }
    comma_ = false;
    return child_step(values_[at_++], machine);
  }

 private:
  const std::vector<Element, Allocator>& values_;
  std::size_t at_ = 0;
  bool opened_ = false;
  bool closed_ = false;
  bool comma_ = false;
};

// An object whose keys are the data, in the map's order -- which for a map
// from strings is the order of their bytes, Canonical JSON's.
template <class Map>
class map_frame {
 public:
  constexpr explicit map_frame(const Map& values)
      : values_(values), at_(values.begin()) {}

  template <class Machine>
  constexpr step next(Machine& machine) {
    if (!opened_) {
      opened_ = true;
      return step::piece("{");
    }
    if (at_ == values_.end()) {
      if (closed_) return step::done();
      closed_ = true;
      return step::piece("}");
    }
    // Each entry: a comma after the first, the key, a colon, the value.
    if (stage_ == 0) {
      stage_ = 1;
      if (at_ != values_.begin()) return step::piece(",");
    }
    if (stage_ == 1) {
      stage_ = 2;
      return step::string(at_->first);
    }
    if (stage_ == 2) {
      stage_ = 3;
      return step::piece(":");
    }
    stage_ = 0;
    return child_step((at_++)->second, machine);
  }

 private:
  const Map& values_;
  typename Map::const_iterator at_;
  int stage_ = 0;
  bool opened_ = false;
  bool closed_ = false;
};

// An object: each key in its order with what is around it, then its value.
template <class Type>
class object_frame {
 public:
  constexpr explicit object_frame(const Type& value) : value_(value) {}

  template <class Machine>
  constexpr step next(Machine& machine) {
    constexpr std::size_t size = schema<Type>::size;
    if (!opened_) {
      opened_ = true;
      return step::piece("{");
    }
    if (!keyed_) {
      // An empty optional is no key at all.
      while (at_ != size && !present(at_)) ++at_;
      if (at_ == size) {
        if (closed_) return step::done();
        closed_ = true;
        return step::piece("}");
      }
      if (written_ != 0 && !comma_) {
        comma_ = true;
        return step::piece(",");
      }
      comma_ = false;
      keyed_ = true;
      return step::piece(key(at_));
    }
    keyed_ = false;
    ++written_;
    return member(at_++, machine);
  }

 private:
  template <std::size_t... Rank>
  static constexpr std::string_view key_of(std::size_t rank, std::index_sequence<Rank...>) {
    std::string_view found;
    (void)((rank == Rank ? (found = key_literal<Type, Rank>.view(), true) : false) ||
     ...);
    return found;
  }
  static constexpr std::string_view key(std::size_t rank) {
    return key_of(rank, std::make_index_sequence<schema<Type>::size>{});
  }

  template <class Machine, std::size_t... Rank>
  constexpr step member_of(std::size_t rank, Machine& machine, std::index_sequence<Rank...>) const {
    step found;
    (void)((rank == Rank
          ? (found = child_step(boost::pfr::get<order_of<Type>[Rank]>(value_), machine), true)
          : false) ||
     ...);
    return found;
  }
  template <class Machine>
  constexpr step member(std::size_t rank, Machine& machine) const {
    return member_of(rank, machine, std::make_index_sequence<schema<Type>::size>{});
  }

  template <std::size_t... Rank>
  constexpr bool present_of(std::size_t rank, std::index_sequence<Rank...>) const {
    bool found = true;
    (void)((rank == Rank
                ? (found = [&] {
                     const auto& held =
                         boost::pfr::get<order_of<Type>[Rank]>(value_);
                     if constexpr (is_optional<
                                       std::remove_cvref_t<decltype(held)>>::value) {
                       return held.has_value();
                     } else {
                       return true;
                     }
                   }(),
                   true)
                : false) ||
           ...);
    return found;
  }
  constexpr bool present(std::size_t rank) const {
    if constexpr (keeps_rest<Type>) {
      if (order_of<Type>[rank] == schema_of<Type>.rest_member()) return false;
    }
    return present_of(rank, std::make_index_sequence<schema<Type>::size>{});
  }

  const Type& value_;
  std::size_t at_ = 0;
  std::size_t written_ = 0;
  bool opened_ = false;
  bool keyed_ = false;
  bool comma_ = false;
  bool closed_ = false;
};

// Merging unknown keys still needs a tree. Its address stays stable when the
// traversal stack grows; ordinary typed values never take this path.
class owned_frame {
 public:
  explicit owned_frame(knot::value tree)
      : tree_(std::make_unique<knot::value>(std::move(tree))) {}

  template <class Machine>
  constexpr step next(Machine& machine) {
    if (started_) return step::done();
    started_ = true;
    return child_step(*tree_, machine);
  }

 private:
  std::unique_ptr<knot::value> tree_;
  bool started_ = false;
};

template <class Type, class Machine>
constexpr step child_step(const Type& value, Machine& machine) {
  if constexpr (requires { value.reading; value.data(); }) {
    if (!value.unknown.is_null())
      return machine.template enter<owned_frame>(as_tree(value));
    return spl::visit([&](const auto& held) { return child_step(held, machine); },
                         value.data());
  } else if constexpr (std::same_as<Type, knot::value>) {
    return spl::visit([&](const auto& held) { return child_step(held, machine); },
                         value.data());
  } else if constexpr (std::same_as<Type, knot::raw>) {
    return step::piece(value.text);
  } else if constexpr (std::same_as<Type, std::nullptr_t>) {
    return step::piece("null");
  } else if constexpr (std::same_as<Type, double>) {
    return step::fraction(value);
  } else if constexpr (std::same_as<Type, std::string>) {
    return step::string(value);
  } else if constexpr (is_choice<Type>::value) {
    return step::string(choice<Type>::name(value));
  } else if constexpr (std::same_as<Type, bool>) {
    return step::piece(value ? "true" : "false");
  } else if constexpr (json_integer<Type>) {
    if constexpr (std::is_signed_v<Type>) return step::whole(static_cast<std::int64_t>(value));
    else return step::whole(static_cast<std::uint64_t>(value));
  } else if constexpr (is_optional<Type>::value) {
    return value ? child_step(*value, machine) : step::piece("null");
  } else if constexpr (is_map<Type>::value) {
    return machine.template enter<map_frame<Type>>(value);
  } else if constexpr (is_vector<Type>::value) {
    return machine.template enter<array_frame<typename Type::value_type,
                                             typename Type::allocator_type>>(value);
  } else if constexpr (described<Type>) {
    if constexpr (keeps_rest<Type>) {
      const auto& kept = boost::pfr::get<schema_of<Type>.rest_member()>(value);
      if (!rest_empty(kept)) return machine.template enter<owned_frame>(to_value(value));
    }
    return machine.template enter<object_frame<Type>>(value);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

template <class... Types>
struct types {};
template <class... Left, class... Right>
consteval types<Left..., Right...> operator+(types<Left...>, types<Right...>) { return {}; }

template <name Tag, class... Alternatives>
consteval auto alternatives_of(tagged<Tag, Alternatives...>*) {
  return types<Alternatives..., knot::value>{};
}

// Walk the type graph once, including cycles through vectors or value. Only
// frames reachable from this document belong to its machine's variant.
template <class Type>
consteval auto children_of() {
  if constexpr (is_optional<Type>::value || is_vector<Type>::value) {
    return types<typename Type::value_type>{};
  } else if constexpr (is_map<Type>::value) {
    return types<typename Type::mapped_type>{};
  } else if constexpr (std::same_as<Type, knot::value>) {
    return types<knot::value::array, knot::value::object>{};
  } else if constexpr (requires(const Type& value) { value.reading; value.data(); }) {
    return alternatives_of(static_cast<Type*>(nullptr));
  } else if constexpr (described<Type>) {
    auto members = []<std::size_t... At>(std::index_sequence<At...>) {
      return types<std::remove_cvref_t<decltype(boost::pfr::get<At>(
          std::declval<const Type&>()))>...>{};
    }(std::make_index_sequence<schema<Type>::size>{});
    if constexpr (keeps_rest<Type>) return members + types<knot::value>{};
    else return members;
  } else {
    return types<>{};
  }
}

template <class Pending, class Seen = types<>>
struct reachable;
template <class... Seen>
struct reachable<types<>, types<Seen...>> { using type = types<Seen...>; };
template <class Type, class... Rest, class... Seen>
struct reachable<types<Type, Rest...>, types<Seen...>> {
  static consteval auto next() {
    if constexpr ((std::same_as<Type, Seen> || ...)) {
      return std::type_identity<reachable<types<Rest...>, types<Seen...>>>{};
    } else {
      return std::type_identity<reachable<decltype(children_of<Type>() + types<Rest...>{}),
                                          types<Seen..., Type>>>{};
    }
  }
  using type = typename decltype(next())::type::type;
};

template <class Type>
consteval auto frames_of() {
  if constexpr (std::same_as<Type, knot::value>) return types<owned_frame>{};
  else if constexpr (is_map<Type>::value) return types<map_frame<Type>>{};
  else if constexpr (is_vector<Type>::value)
    return types<array_frame<typename Type::value_type, typename Type::allocator_type>>{};
  else if constexpr (described<Type>) return types<object_frame<Type>>{};
  else return types<>{};
}

template <class... Frames>
consteval auto variant_of(types<Frames...>) {
  return std::type_identity<std::variant<Frames...>>{};
}
template <class... Types>
consteval auto state_of(types<Types...>) {
  return variant_of((types<std::monostate>{} + ... + frames_of<Types>()));
}

// Eight nested containers fit inline. Deeper documents grow one contiguous
// overflow stack, reused across siblings, instead of allocating each frame.
template <class Root>
class machine {
  using state = typename decltype(state_of(typename reachable<types<Root>>::type{}))::type;
  static constexpr std::size_t inline_depth = 8;

 public:
  constexpr void start(const Root& value) {
    reset();
    first_ = child_step(value, *this);
  }

  constexpr void reset() {
    overflow_.clear();
    while (depth_ != 0) inline_[--depth_].reset();
    pending_.reset();
    string_.reset();
    first_.reset();
  }

  template <class Frame, class Value>
  constexpr step enter(Value&& value) {
    pending_.emplace(std::in_place_type<Frame>, std::forward<Value>(value));
    return step::into();
  }

  constexpr std::optional<std::string_view> next() {
    for (;;) {
      if (string_) {
        const step one = string_->next();
        if (one.kind == step::what::done) {
          string_.reset();
          continue;
        }
        if (!one.text.empty()) return one.text;
        continue;
      }
      step one;
      if (first_) {
        one = *first_;
        first_.reset();
      } else {
        if (depth_ == 0) return std::nullopt;
        auto& top = overflow_.empty() ? *inline_[depth_ - 1] : overflow_.back();
        one = std::visit([&](auto& frame) -> step {
          if constexpr (std::same_as<std::remove_cvref_t<decltype(frame)>, std::monostate>)
            return step::done();
          else return frame.next(*this);
        }, top);
      }
      switch (one.kind) {
        case step::what::piece:
          if (!one.text.empty()) return one.text;
          break;
        case step::what::child:
          // Grow only after next() returns, so no visited frame is relocated
          // while its member function is running.
          if (depth_ < inline_depth) inline_[depth_++].emplace(std::move(*pending_));
          else overflow_.push_back(std::move(*pending_));
          pending_.reset();
          break;
        case step::what::done:
          if (!overflow_.empty()) overflow_.pop_back();
          else if (depth_ != 0) inline_[--depth_].reset();
          break;
        case step::what::string:
          string_.emplace(one.text);
          break;
        case step::what::integer: {
          const auto made = std::to_chars(digits_.data(), digits_.data() + digits_.size(),
                                          one.integer);
          return std::string_view(digits_.data(), made.ptr);
        }
        case step::what::natural: {
          const auto made = std::to_chars(digits_.data(), digits_.data() + digits_.size(),
                                          one.natural);
          return std::string_view(digits_.data(), made.ptr);
        }
        case step::what::floating: {
          const auto made = std::to_chars(digits_.data(), digits_.data() + digits_.size(),
                                          one.floating);
          return std::string_view(digits_.data(), made.ptr);
        }
      }
    }
  }

 private:
  std::array<std::optional<state>, inline_depth> inline_;
  std::vector<state> overflow_;
  std::size_t depth_ = 0;
  std::optional<state> pending_;
  std::optional<step> first_;
  std::optional<string_frame> string_;
  std::array<char, 32> digits_{};
};

}  // namespace knot::detail::lazy

export namespace knot {

// A value as Canonical JSON, lazily: a view of its characters, made as they
// are pulled. A value given as an rvalue is kept by the view; one given as an
// lvalue is referred to, and has to outlive it.
template <document Type>
class json_view : public std::ranges::view_interface<json_view<Type>> {
 public:
  class iterator {
   public:
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;

    iterator() = default;
    constexpr explicit iterator(json_view* view) : view_(view) { view_->pull(); }
    iterator(iterator&&) = default;
    iterator& operator=(iterator&&) = default;

    constexpr char operator*() const { return view_->piece_[view_->at_]; }
    constexpr iterator& operator++() {
      if (++view_->at_ == view_->piece_.size()) view_->pull();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    friend constexpr bool operator==(const iterator& one, std::default_sentinel_t) {
      return one.view_->done_;
    }

   private:
    json_view* view_ = nullptr;
  };

  // The pieces the characters come in.
  class chunk_view : public std::ranges::view_interface<chunk_view> {
   public:
    class iterator {
     public:
      using value_type = std::string_view;
      using difference_type = std::ptrdiff_t;
      using iterator_concept = std::input_iterator_tag;

      iterator() = default;
      constexpr explicit iterator(json_view* view) : view_(view) { view_->pull(); }
      iterator(iterator&&) = default;
      iterator& operator=(iterator&&) = default;

      constexpr std::string_view operator*() const { return view_->piece_; }
      constexpr iterator& operator++() {
        view_->pull();
        return *this;
      }
      constexpr void operator++(int) { ++*this; }
      friend constexpr bool operator==(const iterator& one, std::default_sentinel_t) {
        return one.view_->done_;
      }

     private:
      json_view* view_ = nullptr;
    };

    constexpr explicit chunk_view(json_view* view) : view_(view) {}
    constexpr iterator begin() {
      view_->start();
      return iterator(view_);
    }
    constexpr std::default_sentinel_t end() const noexcept { return {}; }

   private:
    json_view* view_;
  };

  constexpr explicit json_view(const Type& value) : value_(&value) {}
  constexpr explicit json_view(Type&& value)
      : owned_(std::in_place, std::move(value)), value_(&*owned_) {}

  // Moving a view invalidates its iterators. begin() starts a fresh traversal,
  // with every reference rebound to the destination's inline-owned value.
  constexpr json_view(json_view&& other) {
    if (other.owned_) owned_.emplace(std::move(*other.owned_));
    value_ = owned_ ? &*owned_ : other.value_;
    other.machine_.reset();
  }
  constexpr json_view& operator=(json_view&& other) {
    if (this == &other) return *this;
    machine_.reset();
    other.machine_.reset();
    owned_.reset();
    if (other.owned_) owned_.emplace(std::move(*other.owned_));
    value_ = owned_ ? &*owned_ : other.value_;
    piece_ = {};
    at_ = 0;
    done_ = true;
    return *this;
  }

  constexpr iterator begin() {
    start();
    return iterator(this);
  }
  constexpr std::default_sentinel_t end() const noexcept { return {}; }

  // The same document, as the pieces it is made in.
  constexpr chunk_view chunks() { return chunk_view(this); }

 private:
  constexpr void start() {
    machine_.start(*value_);
    done_ = false;
  }
  constexpr void pull() {
    at_ = 0;
    if (const auto next = machine_.next()) {
      piece_ = *next;
    } else {
      done_ = true;
    }
  }

  std::optional<Type> owned_;
  const Type* value_ = nullptr;
  detail::lazy::machine<Type> machine_;
  std::string_view piece_;
  std::size_t at_ = 0;
  bool done_ = true;
};

template <class Type>
  requires document<std::remove_cvref_t<Type>>
constexpr json_view<std::remove_cvref_t<Type>> to_json(Type&& value) {
  return json_view<std::remove_cvref_t<Type>>(std::forward<Type>(value));
}

// A value to be formatted as its JSON: std::format("{}", knot::as_json{value})
// writes it through the format's own output, piece by piece, as write()
// does -- no string made first. It refers to the value, which outlives it.
template <class Type>
struct as_json {
  const Type& value;
};
template <class Type>
as_json(const Type&) -> as_json<Type>;

}  // namespace knot

namespace knot::detail::eager {

// What a byte that needs escaping is written as.
constexpr std::string_view escape_of(char letter, std::array<char, 6>& room) {
  switch (letter) {
    case '"': return "\\\"";
    case '\\': return "\\\\";
    case '\b': return "\\b";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    default: {
      constexpr std::string_view digits = "0123456789abcdef";
      const auto byte = static_cast<unsigned char>(letter);
      room = {'\\', 'u', '0', '0', digits[byte >> 4], digits[byte & 0xf]};
      return {room.data(), room.size()};
    }
  }
}

template <class Out>
constexpr void put_string(Out& out, std::string_view text) {
  out += '"';
  while (!text.empty()) {
    const std::size_t run = string_run(text);
    out.append(text.substr(0, run));
    text.remove_prefix(run);
    if (!text.empty()) {
      std::array<char, 6> room{};
      out.append(escape_of(text.front(), room));
      text.remove_prefix(1);
    }
  }
  out += '"';
}

template <class Out, class Type>
constexpr void put(Out& out, const Type& value);

template <class Out, class Number>
constexpr void put_number(Out& out, Number number) {
  std::array<char, 32> digits{};
  const auto made = std::to_chars(digits.data(), digits.data() + digits.size(), number);
  out.append(std::string_view(digits.data(), made.ptr));
}

template <class Out, class Type>
constexpr void put(Out& out, const Type& value) {
  if constexpr (requires { value.reading; value.data(); }) {
    if (!value.unknown.is_null()) {
      put(out, as_tree(value));
    } else {
      spl::visit([&](const auto& held) { put(out, held); }, value.data());
    }
  } else if constexpr (std::same_as<Type, knot::value>) {
    spl::visit([&](const auto& held) { put(out, held); }, value.data());
  } else if constexpr (std::same_as<Type, knot::raw>) {
    out += value.text;
  } else if constexpr (std::same_as<Type, std::nullptr_t>) {
    out += "null";
  } else if constexpr (std::same_as<Type, bool>) {
    out += value ? "true" : "false";
  } else if constexpr (std::same_as<Type, double> || json_integer<Type>) {
    put_number(out, value);
  } else if constexpr (std::same_as<Type, std::string>) {
    put_string(out, value);
  } else if constexpr (is_choice<Type>::value) {
    put_string(out, choice<Type>::name(value));
  } else if constexpr (is_optional<Type>::value) {
    if (value) put(out, *value);
    else out += "null";
  } else if constexpr (is_vector<Type>::value) {
    out += '[';
    bool first = true;
    for (const auto& one : value) {
      if (!first) out += ',';
      first = false;
      put(out, one);
    }
    out += ']';
  } else if constexpr (is_map<Type>::value) {
    out += '{';
    bool first = true;
    for (const auto& [key, one] : value) {
      if (!first) out += ',';
      first = false;
      put_string(out, key);
      out += ':';
      put(out, one);
    }
    out += '}';
  } else if constexpr (described<Type>) {
    if constexpr (keeps_rest<Type>) {
      const auto& kept = boost::pfr::get<schema_of<Type>.rest_member()>(value);
      if (!rest_empty(kept)) {
        put(out, to_value(value));
        return;
      }
    }
    out += '{';
    bool first = true;
    [&]<std::size_t... Rank>(std::index_sequence<Rank...>) {
      (([&] {
         if constexpr (keeps_rest<Type>) {
           if (order_of<Type>[Rank] == schema_of<Type>.rest_member()) return;
         }
         const auto& member = boost::pfr::get<order_of<Type>[Rank]>(value);
         if constexpr (is_optional<std::remove_cvref_t<decltype(member)>>::value) {
           if (!member) return;
         }
         if (!first) out += ',';
         first = false;
         out.append(key_literal<Type, Rank>.view());
         put(out, member);
       }()),
       ...);
    }(std::make_index_sequence<schema<Type>::size>{});
    out += '}';
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

// The same typed writer can hand each piece directly to a concrete sink.
// Pieces are borrowed only for the duration of the call.
template <class Sink>
class chunk_output {
 public:
  constexpr explicit chunk_output(Sink& sink) : sink_(sink) {}
  constexpr void append(std::string_view text) {
    if (!text.empty()) std::invoke(sink_, text);
  }
  constexpr void operator+=(std::string_view text) { append(text); }
  constexpr void operator+=(char letter) { append({&letter, 1}); }

 private:
  Sink& sink_;
};

}  // namespace knot::detail::eager

export namespace knot {

// Push pieces to a file, hash or other sink without buffering the document or
// constructing a lazy traversal stack. The sink consumes each borrowed piece
// before returning; exceptions propagate to the caller.
template <class Sink, class Type>
  requires document<std::remove_cvref_t<Type>> && std::invocable<Sink&, std::string_view>
constexpr void write_chunks(Sink&& sink, const Type& value) {
  detail::eager::chunk_output out(sink);
  detail::eager::put(out, value);
}

// The output iterator past the last character is returned. This shares the
// direct writer with write_chunks(), including its allocation-free traversal.
template <class Type, class Out>
  requires document<std::remove_cvref_t<Type>> && std::output_iterator<Out, char>
constexpr Out write(Out out, const Type& value) {
  write_chunks([&](std::string_view piece) {
    out = std::ranges::copy(piece, std::move(out)).out;
  }, value);
  return out;
}

// A value as Canonical JSON, added to the end of a string at once: no view
// and no pieces, for where the whole is wanted anyway.
template <class Type>
  requires document<std::remove_cvref_t<Type>>
constexpr void write(std::string& out, const Type& value) {
  detail::eager::put(out, value);
}

template <class Type>
  requires document<std::remove_cvref_t<Type>>
constexpr std::string to_json_string(const Type& value) {
  std::string out;
  write(out, value);
  return out;
}

// The same JSON laid out for a person to read: each member and element on a
// line of its own, indented by `indent` spaces a level, a space after each
// colon; an empty object or array stays {} or []. What is written is what
// write writes -- the same members in the same order, the same strings --
// only spread out: it is laid out from that text, a string's insides passed
// over as they are.
constexpr void lay_out(std::string& out, std::string_view json, int indent = 2) {
  int depth = 0;
  bool in_string = false;
  const auto new_line = [&] {
    out += '\n';
    out.append(static_cast<std::size_t>(depth * indent), ' ');
  };
  for (std::size_t at = 0; at < json.size(); ++at) {
    const char c = json[at];
    if (in_string) {
      out += c;
      if (c == '\\' && at + 1 < json.size())
        out += json[++at];
      else if (c == '"')
        in_string = false;
      continue;
    }
    switch (c) {
      case '"':
        in_string = true;
        out += c;
        break;
      case '{':
      case '[':
        out += c;
        // Empty: kept on the line it opens.
        if (at + 1 < json.size() && (json[at + 1] == '}' || json[at + 1] == ']')) {
          out += json[++at];
          break;
        }
        ++depth;
        new_line();
        break;
      case '}':
      case ']':
        --depth;
        new_line();
        out += c;
        break;
      case ',':
        out += c;
        new_line();
        break;
      case ':':
        out += ": ";
        break;
      default:
        out += c;
    }
  }
}

template <class Type>
  requires document<std::remove_cvref_t<Type>>
constexpr void write_pretty(std::string& out, const Type& value, int indent = 2) {
  lay_out(out, to_json_string(value), indent);
}

template <class Type>
  requires document<std::remove_cvref_t<Type>>
constexpr std::string to_pretty_json_string(const Type& value, int indent = 2) {
  std::string out;
  write_pretty(out, value, indent);
  return out;
}

}  // namespace knot

// std::format's way to it: the JSON written through the context's iterator.
template <class Type>
  requires knot::document<std::remove_cvref_t<Type>>
struct std::formatter<knot::as_json<Type>, char> {
  constexpr auto parse(std::format_parse_context& context) { return context.begin(); }
  template <class Context>
  auto format(const knot::as_json<Type>& json, Context& context) const {
    return knot::write(context.out(), json.value);
  }
};
