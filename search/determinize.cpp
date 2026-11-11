#include "search/determinize.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <optional>

#include "poly/rules.hpp"

namespace poly::search {

namespace {

void with_parents(TribeState& t, Tech tech) {
    for (std::optional<Tech> k = tech; k && !t.has_tech(*k);
         k = kTechParent[static_cast<std::size_t>(*k)]) {
        mark_researched(t, *k);
    }
}

// What the observation shows tribe q must have researched.
void evident_techs(const GameState& s, int q, TribeState& t) {
    if (const auto start = kStartTech[static_cast<std::size_t>(t.type)]) {
        with_parents(t, *start);
    }
    for (const Slot u : t.tribe_units) {
        if (const auto need = kUnitTech[static_cast<std::size_t>(s.units[u].type)]) {
            with_parents(t, *need);
        }
    }
    const auto tiles = static_cast<std::size_t>(s.side) * static_cast<std::size_t>(s.side);
    for (std::size_t i = 0; i < tiles; ++i) {
        const Slot c = s.city_owner[i];
        if (c == kNoSlot || s.cities[c].tribe != q || s.building[i] == Building::None) {
            continue;
        }
        if (const auto need = kBuildingTech[static_cast<std::size_t>(s.building[i])]) {
            with_parents(t, *need);
        }
    }
}

}  // namespace

GameState determinize(const Observation& obs, Rng& rng, Chance& chance) {
    GameState s = obs.view();
    for (int q = 0; q < s.num_tribes; ++q) {
        if (q == obs.observer()) {
            continue;
        }
        TribeState& t = s.tribes[static_cast<std::size_t>(q)];
        evident_techs(s, q, t);
        const int target = 1 + (s.tick / 2);
        for (int have = std::popcount(t.techs); have < target && !t.all_researched;) {
            research_random(t, chance);
            const int now = std::popcount(t.techs);
            if (now == have) {
                break;  // nothing researchable (can't happen before all are researched)
            }
            have = now;
        }
        int income = 0;
        for (const Slot c : t.cities) {
            City& city = s.cities[c];
            const int need = std::max<int>(city.population_need, 1);  // reaching it levels up
            city.population =
                static_cast<std::int16_t>(rng.below(static_cast<std::uint32_t>(need)));
            income += city_production(city);
        }
        t.stars = income > 0 ? income * static_cast<std::int32_t>(rng.below(3)) : 0;
    }
    return s;
}

}  // namespace poly::search
