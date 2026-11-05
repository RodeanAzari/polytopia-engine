#pragma once

#include <array>
#include <cstdint>

namespace poly {

// The single source of randomness for the simulator and search (CLAUDE.md §5: every random draw
// goes through one seeded RNG passed explicitly). xoshiro256** seeded via SplitMix64.
// A plain value type: copying it forks the stream, which search relies on when cloning states.
class Rng {
   public:
    explicit constexpr Rng(std::uint64_t seed) noexcept {
        for (auto& word : s_) {
            seed += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
            word = z ^ (z >> 31U);
        }
    }

    constexpr std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17U;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform integer in [0, bound), unbiased (Lemire's method). bound must be > 0.
    constexpr std::uint32_t below(std::uint32_t bound) noexcept {
        std::uint64_t m = (next() >> 32U) * bound;
        auto low = static_cast<std::uint32_t>(m);
        if (low < bound) {
            const std::uint32_t threshold = -bound % bound;
            while (low < threshold) {
                m = (next() >> 32U) * bound;
                low = static_cast<std::uint32_t>(m);
            }
        }
        return static_cast<std::uint32_t>(m >> 32U);
    }

    friend constexpr bool operator==(const Rng&, const Rng&) = default;

   private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    std::array<std::uint64_t, 4> s_{};
};

}  // namespace poly
