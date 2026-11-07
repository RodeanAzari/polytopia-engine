#pragma once

// Random outcomes in the rules (ADR-005 §3). Rules never draw raw random numbers: they ask a
// Chance for a semantic outcome over an explicit domain. SampledChance draws from poly::Rng; the
// difftest replays Tribes' logged outcomes and checks each request's kind and domain.

#include <cstdint>
#include <span>

#include "poly/rng.hpp"
#include "poly/types.hpp"

namespace poly {

// Ruin outcomes, in the order of the uniform draw (03b §5).
enum class RuinBonus : std::uint8_t { SuperUnit, Research, PopGrowth, Explorer, Resources };
inline constexpr int kNumRuinBonuses = 5;

class Chance {
   public:
    Chance() = default;
    Chance(const Chance&) = default;
    Chance& operator=(const Chance&) = default;
    Chance(Chance&&) = default;
    Chance& operator=(Chance&&) = default;
    virtual ~Chance() = default;

    // Uniform over the 5 outcomes; never Research when exclude_research (03b §5).
    virtual RuinBonus ruin_bonus(bool exclude_research) = 0;
    // Index into `count` candidate techs (enum order), uniform (03b §5, 03b §1).
    virtual int random_tech(int count) = 0;
    // Index into the `count` clipped neighbours of the explorer's tile, uniform (03b §6).
    virtual int explorer_neighbour(int count) = 0;
    // Uniform random permutation of a city list, in place (03b §3).
    virtual void shuffle_cities(std::span<std::uint16_t> cities) = 0;
};

class SampledChance final : public Chance {
   public:
    explicit SampledChance(std::uint64_t seed) : rng_(seed) {}

    RuinBonus ruin_bonus(bool exclude_research) override {
        if (exclude_research) {
            constexpr std::array<RuinBonus, 4> kOthers = {RuinBonus::SuperUnit,
                                                          RuinBonus::PopGrowth, RuinBonus::Explorer,
                                                          RuinBonus::Resources};
            return kOthers[rng_.below(4)];
        }
        return static_cast<RuinBonus>(rng_.below(kNumRuinBonuses));
    }
    int random_tech(int count) override {
        return static_cast<int>(rng_.below(static_cast<std::uint32_t>(count)));
    }
    int explorer_neighbour(int count) override {
        return static_cast<int>(rng_.below(static_cast<std::uint32_t>(count)));
    }
    void shuffle_cities(std::span<std::uint16_t> cities) override {
        for (std::size_t i = cities.size(); i > 1; --i) {
            std::swap(cities[i - 1], cities[rng_.below(static_cast<std::uint32_t>(i))]);
        }
    }

   private:
    Rng rng_;
};

}  // namespace poly
