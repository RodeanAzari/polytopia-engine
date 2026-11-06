#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <string>

namespace poly {

// Fails loudly: capacity overflows and broken invariants abort with a message (ADR-005: never drop
// data silently). Active in every build type.
[[noreturn]] inline void check_failed(const char* msg, const std::source_location& loc) {
    const std::string text = std::string(loc.file_name()) + ":" + std::to_string(loc.line()) +
                             ": check failed: " + msg + "\n";
    std::fputs(text.c_str(), stderr);
    std::abort();
}

inline void check(bool ok, const char* msg,
                  const std::source_location loc = std::source_location::current()) {
    if (!ok) {
        check_failed(msg, loc);
    }
}

// Fixed-capacity vector with the order semantics the rules rely on (rule facts: "last unit" moves,
// list order decides iteration): append at the end, order-preserving removal. Trivially copyable
// when T is, so GameState stays a single memcpy.
template <typename T, std::size_t N>
class InlineVector {
   public:
    using value_type = T;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    constexpr void push_back(const T& v) {
        check(size_ < N, "InlineVector capacity exceeded");
        items_[size_++] = v;
    }

    // Removes the element at index i, shifting later elements down (Java ArrayList.remove(int)).
    constexpr void erase_at(std::size_t i) {
        check(i < size_, "InlineVector index out of range");
        for (std::size_t k = i + 1; k < size_; ++k) {
            items_[k - 1] = items_[k];
        }
        --size_;
    }

    // Removes the first element equal to v (Java ArrayList.remove(Object)); returns whether found.
    constexpr bool erase_value(const T& v) {
        for (std::size_t i = 0; i < size_; ++i) {
            if (items_[i] == v) {
                erase_at(i);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool contains(const T& v) const {
        for (std::size_t i = 0; i < size_; ++i) {
            if (items_[i] == v) {
                return true;
            }
        }
        return false;
    }

    constexpr void clear() noexcept { size_ = 0; }

    constexpr T& operator[](std::size_t i) {
        check(i < size_, "InlineVector index out of range");
        return items_[i];
    }
    constexpr const T& operator[](std::size_t i) const {
        check(i < size_, "InlineVector index out of range");
        return items_[i];
    }
    constexpr T& back() { return (*this)[size_ - 1]; }

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): iterators stay inside the
    // container
    [[nodiscard]] constexpr T* begin() noexcept { return items_.data(); }
    [[nodiscard]] constexpr T* end() noexcept { return items_.data() + size_; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return items_.data(); }
    [[nodiscard]] constexpr const T* end() const noexcept { return items_.data() + size_; }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

   private:
    std::array<T, N> items_{};
    std::size_t size_ = 0;
};

}  // namespace poly
