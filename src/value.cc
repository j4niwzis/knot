// knot.value: any JSON at all, for what no type describes.
//
//   struct event { std::string type; knot::value content; };
//
// The content of an event of a type nobody here knows, or of one that is read
// before its type is: null, true and false, an integer, a number with a
// fraction or an exponent (ordinary JSON only), a string, an array, an object.
// Objects keep their keys sorted by their bytes, so a value is written back as
// Canonical JSON wherever it holds nothing Canonical JSON cannot say.
export module knot.value;

import std;

export namespace knot {

class value {
 public:
  using array = std::vector<value>;
  using object = std::map<std::string, value, std::less<>>;
  using variant = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               std::string, array, object>;

  // null
  constexpr value() = default;
  constexpr value(std::nullptr_t) {}
  constexpr value(bool held) : held_(held) {}
  template <std::integral Integer>
    requires(!std::same_as<Integer, bool>)
  constexpr value(Integer held) : held_(static_cast<std::int64_t>(held)) {}
  constexpr value(double held) : held_(held) {}
  constexpr value(std::string held) : held_(std::move(held)) {}
  constexpr value(std::string_view held) : held_(std::string(held)) {}
  constexpr value(const char* held) : held_(std::string(held)) {}
  constexpr value(array held) : held_(std::move(held)) {}
  constexpr value(object held) : held_(std::move(held)) {}

  // What it holds, for std::visit, std::get and std::holds_alternative.
  [[nodiscard]] constexpr const variant& data() const& { return held_; }
  [[nodiscard]] constexpr variant& data() & { return held_; }

  template <class Alternative>
  [[nodiscard]] constexpr bool is() const {
    return std::holds_alternative<Alternative>(held_);
  }
  template <class Alternative>
  [[nodiscard]] constexpr const Alternative& as() const {
    return std::get<Alternative>(held_);
  }
  [[nodiscard]] constexpr bool is_null() const { return is<std::nullptr_t>(); }

  // A member of an object, or null where there is none or this is no object.
  [[nodiscard]] const value& operator[](std::string_view key) const {
    static const value none;
    if (const auto* members = std::get_if<object>(&held_)) {
      if (const auto found = members->find(key); found != members->end()) {
        return found->second;
      }
    }
    return none;
  }

  friend bool operator==(const value&, const value&) = default;

 private:
  variant held_;
};

}  // namespace knot
