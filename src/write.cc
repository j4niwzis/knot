// knot.write: a described value as its Canonical JSON, lazily.
//
//   std::string text = knot::to_json(event) | std::ranges::to<std::string>();
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
import boost.pfr;
export import knot.format;
export import knot.value;
import knot.read;

namespace knot::detail::lazy {

struct step;

// Something being written, asked for what comes next.
struct frame {
  constexpr virtual ~frame() = default;
  constexpr virtual step next() = 0;
};

// What a frame says next: a piece of text, a frame to go into, or that it is
// done.
//
// A string or a number to be written is said as such rather than as a frame
// of its own: the machine writes it with what it keeps for the purpose, so
// that a leaf costs no allocation -- only an array, a map or an object does.
struct step {
  enum class what { piece, child, done, string, integer, floating } kind = what::done;
  std::string_view text;
  std::unique_ptr<frame> child;
  std::int64_t integer = 0;
  double floating = 0;

  static constexpr step piece(std::string_view text) { return {what::piece, text, {}}; }
  static constexpr step into(std::unique_ptr<frame> child) {
    return {what::child, {}, std::move(child)};
  }
  static constexpr step done() { return {}; }
  static constexpr step string(std::string_view text) { return {what::string, text, {}}; }
  static constexpr step whole(std::int64_t number) {
    return {what::integer, {}, {}, number};
  }
  static constexpr step fraction(double number) {
    return {what::floating, {}, {}, 0, number};
  }
};

template <class Type>
constexpr std::unique_ptr<frame> frame_for(const Type& value);

template <class Type>
constexpr step child_step(const Type& value) {
  if constexpr (std::same_as<Type, std::string>) {
    return step::string(value);
  } else if constexpr (std::same_as<Type, bool>) {
    return step::piece(value ? "true" : "false");
  } else if constexpr (std::same_as<Type, std::nullptr_t>) {
    return step::piece("null");
  } else if constexpr (json_integer<Type>) {
    return step::whole(static_cast<std::int64_t>(value));
  } else if constexpr (std::same_as<Type, double>) {
    return step::fraction(value);
  } else if constexpr (is_optional<Type>::value) {
    return child_step(*value);
  } else {
    return step::into(frame_for(value));
  }
}

// A string: its quotes, runs that need nothing, and the escapes between them.
class string_frame final : public frame {
 public:
  constexpr explicit string_frame(std::string_view text) : text_(text) {}

  constexpr step next() override {
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

// One piece and done: a number, true, false.
class piece_frame final : public frame {
 public:
  constexpr explicit piece_frame(std::string_view text) : text_(text) {}
  template <class Integer>
  constexpr explicit piece_frame(Integer value) {
    const auto made = std::to_chars(digits_.data(),
                                    digits_.data() + digits_.size(), value);
    text_ = std::string_view(digits_.data(), made.ptr);
  }

  constexpr step next() override {
    if (said_) return step::done();
    said_ = true;
    return step::piece(text_);
  }

 private:
  std::array<char, 32> digits_{};
  std::string_view text_;
  bool said_ = false;
};

template <class Element, class Allocator>
class array_frame final : public frame {
 public:
  constexpr explicit array_frame(const std::vector<Element, Allocator>& values)
      : values_(values) {}

  constexpr step next() override {
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
    return child_step(values_[at_++]);
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
class map_frame final : public frame {
 public:
  constexpr explicit map_frame(const Map& values)
      : values_(values), at_(values.begin()) {}

  constexpr step next() override {
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
    return child_step((at_++)->second);
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
class object_frame final : public frame {
 public:
  constexpr explicit object_frame(const Type& value) : value_(value) {}

  constexpr step next() override {
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
    return member(at_++);
  }

 private:
  template <std::size_t... Rank>
  static constexpr std::string_view key_of(std::size_t rank, std::index_sequence<Rank...>) {
    std::string_view found;
    ((rank == Rank ? (found = key_literal<Type, Rank>.view(), true) : false) ||
     ...);
    return found;
  }
  static constexpr std::string_view key(std::size_t rank) {
    return key_of(rank, std::make_index_sequence<schema<Type>::size>{});
  }

  template <std::size_t... Rank>
  constexpr step member_of(std::size_t rank, std::index_sequence<Rank...>) const {
    step found;
    ((rank == Rank
          ? (found = child_step(boost::pfr::get<order_of<Type>[Rank]>(value_)), true)
          : false) ||
     ...);
    return found;
  }
  constexpr step member(std::size_t rank) const {
    return member_of(rank, std::make_index_sequence<schema<Type>::size>{});
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

// A tree made for the writing, kept for as long as it is written.
class owned_frame final : public frame {
 public:
  explicit owned_frame(knot::value tree) : tree_(std::move(tree)) {}

  constexpr step next() override {
    if (started_) return step::done();
    started_ = true;
    return step::into(frame_for(tree_));
  }

 private:
  knot::value tree_;
  bool started_ = false;
};

template <class Type>
constexpr std::unique_ptr<frame> frame_for(const Type& value) {
  if constexpr (requires { value.reading; value.data(); }) {
    // A knot::by: the alternative it holds -- with what it did not have laid
    // back in, where there is any.
    if (!value.unknown.is_null()) return std::make_unique<owned_frame>(as_tree(value));
    return std::visit([](const auto& held) { return frame_for(held); },
                      value.data());
  } else if constexpr (std::same_as<Type, knot::value>) {
    return std::visit([](const auto& held) { return frame_for(held); },
                      value.data());
  } else if constexpr (std::same_as<Type, std::nullptr_t>) {
    return std::make_unique<piece_frame>(std::string_view("null"));
  } else if constexpr (std::same_as<Type, double>) {
    // Not Canonical JSON, which has no such numbers: the shortest text that
    // reads back as the same double.
    return std::make_unique<piece_frame>(value);
  } else if constexpr (std::same_as<Type, std::string>) {
    return std::make_unique<string_frame>(value);
  } else if constexpr (std::same_as<Type, bool>) {
    return std::make_unique<piece_frame>(value ? std::string_view("true")
                                               : std::string_view("false"));
  } else if constexpr (json_integer<Type>) {
    return std::make_unique<piece_frame>(value);
  } else if constexpr (is_optional<Type>::value) {
    // Only asked of one that holds something.
    return frame_for(*value);
  } else if constexpr (is_map<Type>::value) {
    return std::make_unique<map_frame<Type>>(value);
  } else if constexpr (is_vector<Type>::value) {
    return std::make_unique<array_frame<typename Type::value_type,
                                        typename Type::allocator_type>>(value);
  } else if constexpr (described<Type>) {
    if constexpr (keeps_rest<Type>) {
      const auto& kept = boost::pfr::get<schema_of<Type>.rest_member()>(value);
      if (!kept.is_null() &&
          !(kept.template is<knot::value::object>() && kept.template as<knot::value::object>().empty())) {
        return std::make_unique<owned_frame>(to_value(value));
      }
    }
    return std::make_unique<object_frame<Type>>(value);
  } else {
    static_assert(false, "knot: this type has no JSON form");
  }
}

// The frames being written, innermost last.
class machine {
 public:
  constexpr machine() = default;
  constexpr explicit machine(std::unique_ptr<frame> outermost) {
    stack_.push_back(std::move(outermost));
  }

  constexpr std::optional<std::string_view> next() {
    for (;;) {
      // A string being written goes first, piece by piece.
      if (string_) {
        const step one = string_->next();
        if (one.kind == step::what::done) {
          string_.reset();
          continue;
        }
        if (!one.text.empty()) return one.text;
        continue;
      }
      if (stack_.empty()) return std::nullopt;
      step one = stack_.back()->next();
      switch (one.kind) {
        case step::what::piece:
          if (!one.text.empty()) return one.text;
          break;
        case step::what::child:
          stack_.push_back(std::move(one.child));
          break;
        case step::what::done:
          stack_.pop_back();
          break;
        case step::what::string:
          string_.emplace(one.text);
          break;
        case step::what::integer: {
          const auto made = std::to_chars(digits_.data(), digits_.data() + digits_.size(),
                                          one.integer);
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
  std::vector<std::unique_ptr<frame>> stack_;
  // What a leaf is written with: no frame of its own.
  std::optional<string_frame> string_;
  std::array<char, 32> digits_{};
};

}  // namespace knot::detail::lazy

export namespace knot {

// A value as Canonical JSON, lazily: a view of its characters, made as they
// are pulled. A value given as an rvalue is kept by the view; one given as an
// lvalue is referred to, and has to outlive it.
template <described Type>
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
      : owned_(std::make_unique<Type>(std::move(value))),
        value_(owned_.get()) {}
  json_view(json_view&&) = default;
  json_view& operator=(json_view&&) = default;

  constexpr iterator begin() {
    start();
    return iterator(this);
  }
  constexpr std::default_sentinel_t end() const noexcept { return {}; }

  // The same document, as the pieces it is made in.
  constexpr chunk_view chunks() { return chunk_view(this); }

 private:
  constexpr void start() {
    machine_ = detail::lazy::machine(detail::lazy::frame_for(*value_));
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

  std::unique_ptr<Type> owned_;
  const Type* value_ = nullptr;
  detail::lazy::machine machine_;
  std::string_view piece_;
  std::size_t at_ = 0;
  bool done_ = true;
};

template <class Type>
  requires described<std::remove_cvref_t<Type>>
constexpr json_view<std::remove_cvref_t<Type>> to_json(Type&& value) {
  return json_view<std::remove_cvref_t<Type>>(std::forward<Type>(value));
}

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

constexpr void put_string(std::string& out, std::string_view text) {
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

template <class Type>
constexpr void put(std::string& out, const Type& value);

template <class Number>
constexpr void put_number(std::string& out, Number number) {
  std::array<char, 32> digits{};
  const auto made = std::to_chars(digits.data(), digits.data() + digits.size(), number);
  out.append(digits.data(), made.ptr);
}

template <class Type>
constexpr void put(std::string& out, const Type& value) {
  if constexpr (requires { value.reading; value.data(); }) {
    if (!value.unknown.is_null()) {
      put(out, as_tree(value));
    } else {
      std::visit([&](const auto& held) { put(out, held); }, value.data());
    }
  } else if constexpr (std::same_as<Type, knot::value>) {
    std::visit([&](const auto& held) { put(out, held); }, value.data());
  } else if constexpr (std::same_as<Type, std::nullptr_t>) {
    out += "null";
  } else if constexpr (std::same_as<Type, bool>) {
    out += value ? "true" : "false";
  } else if constexpr (std::same_as<Type, double> || json_integer<Type>) {
    put_number(out, value);
  } else if constexpr (std::same_as<Type, std::string>) {
    put_string(out, value);
  } else if constexpr (is_optional<Type>::value) {
    put(out, *value);
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
      if (!kept.is_null() &&
          !(kept.template is<knot::value::object>() && kept.template as<knot::value::object>().empty())) {
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

}  // namespace knot::detail::eager

export namespace knot {

// A value as Canonical JSON, added to the end of a string at once: no view
// and no pieces, for where the whole is wanted anyway.
template <class Type>
  requires described<std::remove_cvref_t<Type>>
constexpr void write(std::string& out, const Type& value) {
  detail::eager::put(out, value);
}

template <class Type>
  requires described<std::remove_cvref_t<Type>>
constexpr std::string to_json_string(const Type& value) {
  std::string out;
  write(out, value);
  return out;
}

}  // namespace knot
