#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

// Fixed-capacity data structures for bounded, deterministic kernel storage.

template <typename T, std::size_t CAPACITY>
class FixedVector {
  static_assert(CAPACITY > 0, "FixedVector capacity must be > 0");

public:
  using value_type = T;

  constexpr FixedVector() = default;

  [[nodiscard]] constexpr bool push_back(const T &value) {
    if (size_ >= CAPACITY) {
      assert(false && "FixedVector capacity exceeded");
      return false;
    }
    data_[size_++] = value;
    return true;
  }

  [[nodiscard]] constexpr bool push_back(T &&value) {
    if (size_ >= CAPACITY) {
      assert(false && "FixedVector capacity exceeded");
      return false;
    }
    data_[size_++] = std::move(value);
    return true;
  }

  constexpr void clear() { size_ = 0; }

  [[nodiscard]] constexpr bool erase_first(const T &value) {
    for (std::size_t i = 0; i < size_; ++i) {
      if (data_[i] == value) {
        erase_at(i);
        return true;
      }
    }
    return false;
  }

  constexpr void erase_at(std::size_t idx) {
    assert(idx < size_);
    for (std::size_t i = idx + 1; i < size_; ++i) {
      data_[i - 1] = std::move(data_[i]);
    }
    --size_;
  }

  [[nodiscard]] constexpr std::size_t size() const { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const { return CAPACITY; }
  [[nodiscard]] constexpr bool empty() const { return size_ == 0; }
  [[nodiscard]] constexpr bool full() const { return size_ == CAPACITY; }

  constexpr T &operator[](std::size_t idx) {
    assert(idx < size_);
    return data_[idx];
  }

  constexpr const T &operator[](std::size_t idx) const {
    assert(idx < size_);
    return data_[idx];
  }

  constexpr T *begin() { return data_.data(); }
  constexpr T *end() { return data_.data() + size_; }
  constexpr const T *begin() const { return data_.data(); }
  constexpr const T *end() const { return data_.data() + size_; }

private:
  std::array<T, CAPACITY> data_{};
  std::size_t size_ = 0;
};

template <typename T, std::size_t CAPACITY, typename Compare = std::less<T>>
class FixedMinHeap {
  static_assert(CAPACITY > 0, "FixedMinHeap capacity must be > 0");

public:
  constexpr FixedMinHeap() = default;

  [[nodiscard]] constexpr bool push(const T &value) {
    if (size_ >= CAPACITY) {
      assert(false && "FixedMinHeap capacity exceeded");
      return false;
    }
    data_[size_] = value;
    siftUp(size_);
    ++size_;
    return true;
  }

  [[nodiscard]] constexpr bool pop() {
    if (size_ == 0) {
      return false;
    }
    --size_;
    if (size_ > 0) {
      data_[0] = data_[size_];
      siftDown(0);
    }
    return true;
  }

  [[nodiscard]] constexpr const T &top() const {
    assert(size_ > 0);
    return data_[0];
  }

  [[nodiscard]] constexpr bool empty() const { return size_ == 0; }
  [[nodiscard]] constexpr std::size_t size() const { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const { return CAPACITY; }

  constexpr void clear() { size_ = 0; }

private:
  constexpr void siftUp(std::size_t idx) {
    while (idx > 0) {
      std::size_t parent = (idx - 1) / 2;
      if (!comp_(data_[idx], data_[parent])) {
        break;
      }
      std::swap(data_[idx], data_[parent]);
      idx = parent;
    }
  }

  constexpr void siftDown(std::size_t idx) {
    while (true) {
      std::size_t left = idx * 2 + 1;
      std::size_t right = idx * 2 + 2;
      std::size_t smallest = idx;

      if (left < size_ && comp_(data_[left], data_[smallest])) {
        smallest = left;
      }
      if (right < size_ && comp_(data_[right], data_[smallest])) {
        smallest = right;
      }
      if (smallest == idx) {
        break;
      }
      std::swap(data_[idx], data_[smallest]);
      idx = smallest;
    }
  }

  std::array<T, CAPACITY> data_{};
  std::size_t size_ = 0;
  [[no_unique_address]] Compare comp_{};
};

template <typename T, std::size_t CAPACITY>
class FixedObjectPool {
  static_assert(CAPACITY > 0, "FixedObjectPool capacity must be > 0");

public:
  FixedObjectPool() { reset(); }
  
  ~FixedObjectPool() { clear(); }

  FixedObjectPool(const FixedObjectPool &) = delete;
  FixedObjectPool &operator=(const FixedObjectPool &) = delete;

  template <typename... Args>
  [[nodiscard]] T *create(Args &&...args) {
    if (free_top_ == 0) {
      return nullptr;
    }

    std::uint32_t idx = free_list_[--free_top_];
    in_use_[idx] = true;
    void *slot = storage_.data() + (static_cast<std::size_t>(idx) * sizeof(T));
    return new (slot) T(std::forward<Args>(args)...);
  }

  void destroy(T *obj) {
    if (!obj) {
      return;
    }

    std::byte *base = storage_.data();
    std::byte *ptr = reinterpret_cast<std::byte *>(obj);
    std::ptrdiff_t offset = ptr - base;

    if (offset < 0 || (static_cast<std::size_t>(offset) % sizeof(T)) != 0) {
      return; // Invalid pointer
    }

    std::size_t idx = static_cast<std::size_t>(offset) / sizeof(T);
    if (idx >= CAPACITY || !in_use_[idx]) {
      return; // Out of bounds or not allocated
    }

    obj->~T();
    in_use_[idx] = false;
    free_list_[free_top_++] = static_cast<std::uint32_t>(idx);
  }

  [[nodiscard]] std::size_t capacity() const { return CAPACITY; }
  [[nodiscard]] std::size_t available() const { return free_top_; }
  [[nodiscard]] std::size_t inUse() const { return CAPACITY - free_top_; }
  [[nodiscard]] std::size_t size() const { return CAPACITY - free_top_; }
  
  // Clear all objects - properly calls destructors
  void clear() {
    for (std::size_t i = 0; i < CAPACITY; ++i) {
      if (in_use_[i]) {
        T* obj = reinterpret_cast<T*>(storage_.data() + (i * sizeof(T)));
        obj->~T();
        in_use_[i] = false;
      }
    }
    reset();
  }
  
  // Iterator support for inspection
  template<typename Func>
  void forEach(Func&& func) const {
    for (std::size_t i = 0; i < CAPACITY; ++i) {
      if (in_use_[i]) {
        const T* obj = reinterpret_cast<const T*>(storage_.data() + (i * sizeof(T)));
        func(*obj);
      }
    }
  }

private:
  void reset() {
    for (std::size_t i = 0; i < CAPACITY; ++i) {
      free_list_[i] = static_cast<std::uint32_t>(CAPACITY - 1 - i);
      in_use_[i] = false;
    }
    free_top_ = CAPACITY;
  }

  alignas(T) std::array<std::byte, sizeof(T) * CAPACITY> storage_{};
  std::array<std::uint32_t, CAPACITY> free_list_{};
  std::array<bool, CAPACITY> in_use_{};
  std::size_t free_top_ = 0;
};
