#pragma once

#include <cstddef>
#include <type_traits>
#include <vector>

namespace traceloom {

template <typename T>
class Span {
 public:
  using element_type = T;
  using value_type = typename std::remove_cv<T>::type;
  using pointer = T*;
  using reference = T&;
  using iterator = pointer;

  constexpr Span() noexcept = default;
  constexpr Span(pointer data, std::size_t size) noexcept
      : data_(data), size_(size) {}

  template <typename Allocator>
  Span(std::vector<value_type, Allocator>& values) noexcept
      : data_(values.data()), size_(values.size()) {}

  template <typename Allocator>
  Span(const std::vector<value_type, Allocator>& values) noexcept
      : data_(values.data()), size_(values.size()) {}

  constexpr pointer data() const noexcept { return data_; }
  constexpr std::size_t size() const noexcept { return size_; }
  constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr reference operator[](std::size_t index) const noexcept {
    return data_[index];
  }

  constexpr iterator begin() const noexcept { return data_; }
  constexpr iterator end() const noexcept { return data_ + size_; }

 private:
  pointer data_ = nullptr;
  std::size_t size_ = 0;
};

template <typename T>
using ArrayView = Span<const T>;

}  // namespace traceloom
