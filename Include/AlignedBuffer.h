#pragma once

#include <cstddef>
#include <utility>
#include "arithmetic.h"

class AlignedBuffer {
 private:
  unsigned char* data = nullptr;

 public:
  AlignedBuffer() = default;
  inline explicit AlignedBuffer(std::size_t size) : data(AlignedMalloc(size)) {}
  AlignedBuffer(AlignedBuffer const&) = delete;
  inline AlignedBuffer(AlignedBuffer&& other) noexcept : data(other.data) { other.data = nullptr; }
  AlignedBuffer& operator=(AlignedBuffer const&) = delete;
  inline AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
    if (this != &other) {
      swap(*this, other);
      other.reset();
    }
    return *this;
  }
  inline void reset() noexcept(noexcept(AlignedFree(data))) {
    AlignedFree(data);
    data = nullptr;
  }
  inline ~AlignedBuffer() { reset(); };
  inline friend void swap(AlignedBuffer& a, AlignedBuffer& b) noexcept {
    std::swap(a.data, b.data);
  }

  inline unsigned char* operator[](std::size_t i) const& noexcept { return data + i; }
};
