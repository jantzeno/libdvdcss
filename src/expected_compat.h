#ifndef DVDCSS_EXPECTED_COMPAT_H
#define DVDCSS_EXPECTED_COMPAT_H

#include <utility>
#include <variant>

namespace dvdcss_compat {

template <typename E> class unexpected {
public:
  constexpr explicit unexpected(E error) : error_(std::move(error)) {}

  [[nodiscard]] constexpr const E &error() const & noexcept { return error_; }
  [[nodiscard]] constexpr E &error() & noexcept { return error_; }
  [[nodiscard]] constexpr E &&error() && noexcept { return std::move(error_); }

private:
  E error_;
};

template <typename E> unexpected(E) -> unexpected<E>;

template <typename T, typename E> class expected {
public:
  constexpr expected(const T &value) : storage_(std::in_place_index<0>, value) {}
  constexpr expected(T &&value)
      : storage_(std::in_place_index<0>, std::move(value)) {}

  constexpr expected(const unexpected<E> &error)
      : storage_(std::in_place_index<1>, error.error()) {}
  constexpr expected(unexpected<E> &&error)
      : storage_(std::in_place_index<1>, std::move(error).error()) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return storage_.index() == 0;
  }

  constexpr explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] constexpr T &value() & { return std::get<0>(storage_); }
  [[nodiscard]] constexpr const T &value() const & {
    return std::get<0>(storage_);
  }
  [[nodiscard]] constexpr T &&value() && { return std::get<0>(std::move(storage_)); }

  [[nodiscard]] constexpr E &error() & { return std::get<1>(storage_); }
  [[nodiscard]] constexpr const E &error() const & {
    return std::get<1>(storage_);
  }
  [[nodiscard]] constexpr E &&error() && { return std::get<1>(std::move(storage_)); }

private:
  std::variant<T, E> storage_;
};

} // namespace dvdcss_compat

#endif /* DVDCSS_EXPECTED_COMPAT_H */
