#include "poly/rng.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

// Reproducibility is a hard requirement: difftests and published results depend on
// "same seed -> same game". If this breaks, every recorded seed becomes meaningless.
TEST(Rng, SameSeedGivesSameStream) {
    poly::Rng a(42);
    poly::Rng b(42);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(a.next(), b.next());
    }
}

TEST(Rng, DifferentSeedsDiverge) {
    poly::Rng a(1);
    poly::Rng b(2);
    int equal = 0;
    for (int i = 0; i < 100; ++i) {
        equal += static_cast<int>(a.next() == b.next());
    }
    EXPECT_EQ(equal, 0);
}

// Search clones game states (including the RNG) and replays from them; a copy must continue
// the exact same stream as the original, independently of it.
TEST(Rng, CopyForksTheStream) {
    poly::Rng original(7);
    original.next();
    poly::Rng copy = original;
    const std::uint64_t from_copy = copy.next();
    EXPECT_EQ(original.next(), from_copy);
}

TEST(Rng, BelowStaysInRangeAndCoversIt) {
    poly::Rng rng(123);
    std::array<int, 6> counts{};
    for (int i = 0; i < 60000; ++i) {
        const std::uint32_t v = rng.below(6);
        ASSERT_LT(v, 6U);
        ++counts.at(v);
    }
    // Each bucket expects 10000; 5 sigma is about 456. A biased or broken mapping fails this.
    for (const int c : counts) {
        EXPECT_NEAR(c, 10000, 500);
    }
}

TEST(Rng, BelowOneIsAlwaysZero) {
    poly::Rng rng(9);
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(rng.below(1), 0U);
    }
}

}  // namespace
