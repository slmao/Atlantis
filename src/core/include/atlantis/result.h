#pragma once

#include <atlantis/assert.h>

#include <cstddef>
#include <utility>
#include <variant>

namespace atlantis {

// Minimal explicit-ownership result/error type — not a general-purpose
// utility library, just enough for Atlantis Core's own use per
// specs/0001-project-foundation.md. Accessing value()/error() on the
// wrong state is a programmer error (ATLANTIS_CHECK), not a recoverable
// runtime error.
template <typename T, typename E>
class Result {
 public:
  static Result Ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }
  static Result Err(E error) { return Result(std::in_place_index<1>, std::move(error)); }

  [[nodiscard]] bool isOk() const noexcept { return storage_.index() == 0; }
  [[nodiscard]] bool isErr() const noexcept { return storage_.index() == 1; }

  [[nodiscard]] const T& value() const {
    ATLANTIS_CHECK(isOk());
    return std::get<0>(storage_);
  }

  [[nodiscard]] T& value() {
    ATLANTIS_CHECK(isOk());
    return std::get<0>(storage_);
  }

  [[nodiscard]] const E& error() const {
    ATLANTIS_CHECK(isErr());
    return std::get<1>(storage_);
  }

  [[nodiscard]] E& error() {
    ATLANTIS_CHECK(isErr());
    return std::get<1>(storage_);
  }

 private:
  template <std::size_t Index, typename U>
  Result(std::in_place_index_t<Index> tag, U&& value) : storage_(tag, std::forward<U>(value)) {}

  std::variant<T, E> storage_;
};

}  // namespace atlantis
