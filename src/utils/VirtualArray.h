#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace d2bs::utils {

// Demand-paged array backed by VirtualAlloc. Zero-initialized, but physical
// pages are only allocated as elements are accessed. Ideal for large sparse
// arrays where only a fraction of elements are touched (e.g., A* node storage
// on a large grid where only a small area is explored).
//
// Elements are zero-initialized by the OS on first page access. Callers must
// handle zero as the initial state (use a `visited` flag or generation counter
// to distinguish "untouched" from "value is actually zero").
template <typename T>
class VirtualArray {
    T* data_ = nullptr;
    size_t size_ = 0;

   public:
    VirtualArray() = default;

    explicit VirtualArray(size_t count)
        : data_(
              count > 0 && count <= SIZE_MAX / sizeof(T)
                  ? static_cast<T*>(VirtualAlloc(nullptr, count * sizeof(T), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
                  : nullptr),
          size_(data_ ? count : 0) {}

    ~VirtualArray() {
        if (data_)
            VirtualFree(data_, 0, MEM_RELEASE);
    }

    VirtualArray(VirtualArray&& other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    VirtualArray& operator=(VirtualArray&& other) noexcept {
        if (this != &other) {
            if (data_)
                VirtualFree(data_, 0, MEM_RELEASE);
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    VirtualArray(const VirtualArray&) = delete;
    VirtualArray& operator=(const VirtualArray&) = delete;

    T& operator[](size_t idx) { return data_[idx]; }
    const T& operator[](size_t idx) const { return data_[idx]; }
    size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }
    T* Data() { return data_; }
    const T* Data() const { return data_; }
};

}  // namespace d2bs::utils
