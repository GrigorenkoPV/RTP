#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>
#include "arithmetic.h"

class AlignedBuffer {
 private:
  std::size_t size_ = 0;
  unsigned char* data_ = nullptr;

 public:
  AlignedBuffer() = default;
  inline explicit AlignedBuffer(std::size_t size, bool zero = false)
      : size_(size), data_(AlignedMalloc(size)) {
    if (zero) {
      memset(data_, 0, size);
    }
  }
  AlignedBuffer(AlignedBuffer const&) = delete;
  inline AlignedBuffer(AlignedBuffer&& other) noexcept : size_(other.size_), data_(other.data_) {
    other.size_ = 0;
    other.data_ = nullptr;
  }
  AlignedBuffer& operator=(AlignedBuffer const&) = delete;
  inline AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
    if (this != std::addressof(other)) {
      swap(*this, other);
      other.reset();
    }
    return *this;
  }
  inline void reset() noexcept(noexcept(AlignedFree(data_))) {
    size_ = 0;
    AlignedFree(data_);
    data_ = nullptr;
  }
  inline ~AlignedBuffer() { reset(); };
  inline friend void swap(AlignedBuffer& a, AlignedBuffer& b) noexcept {
    std::swap(a.size_, b.size_);
    std::swap(a.data_, b.data_);
  }

  [[nodiscard]] inline unsigned char& operator[](std::size_t i) & noexcept {
    assert(i < size_);
    return data_[i];
  }
  [[nodiscard]] inline unsigned char const& operator[](std::size_t i) const& noexcept {
    assert(i < size_);
    return data_[i];
  }

  [[nodiscard]] inline unsigned char* data() & noexcept { return data_; }
  [[nodiscard]] inline unsigned char const* data() const& noexcept { return data_; }
  [[nodiscard]] inline std::size_t size() const noexcept { return size_; }

  [[nodiscard]] inline bool isZero() const noexcept {
    for (std::size_t i = 0; i < size_; ++i) {
      if (data_[i]) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] inline friend bool operator==(AlignedBuffer const& a,
                                              AlignedBuffer const& b) noexcept {
    if (a.size_ != b.size_) {
      return false;
    }
    if (a.data() == b.data()) {
      assert(std::addressof(a) == std::addressof(b));
      return true;
    }
    for (std::size_t i = 0; i < a.size_; ++i) {
      if (a[i] != b[i]) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] inline friend bool operator!=(AlignedBuffer const& a,
                                              AlignedBuffer const& b) noexcept {
    return !(a == b);
  }

  inline AlignedBuffer& operator^=(AlignedBuffer const& other) & {
    assert(this->size() >= other.size());
    if (this == std::addressof(other)) {
      memset(this->data(), 0, this->size());
    } else {
      XOR(this->data(), other.data(), other.size());
    }
    return *this;
  }

  // Not a copy constructor just to be explicit.
  [[nodiscard]] inline AlignedBuffer clone() const& {
    auto result = AlignedBuffer(this->size(), false);
    memcpy(result.data(), this->data(), this->size());
    assert(*this == result);
    return result;
  }
};
