#pragma once

// Tribes' multiplicative tie-break noise for search values (agents sheets 02 §3, 03 §1):
// (u + ε)(1 + ε(r − 0.5)), ε = 1e-6, r uniform in [0, 1), one draw per value.

#include "poly/rng.hpp"

namespace poly::search {

inline double noisy(double u, Rng& rng) {
    constexpr double kEps = 1e-6;
    const double r = static_cast<double>(rng.next() >> 11U) * 0x1.0p-53;
    return (u + kEps) * (1.0 + (kEps * (r - 0.5)));
}

}  // namespace poly::search
