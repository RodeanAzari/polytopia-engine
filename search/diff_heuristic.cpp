#include "search/diff_heuristic.hpp"

#include <bit>
#include <cstddef>

#include "poly/rules.hpp"

namespace poly::search {

namespace {

// 01 §4.1 without the score term: 5·production + 4·techs + 4·cities + 2·units + 3·kills
// + 2·Σ levels, for tribe t as `s` has it. All integer, so its difference is exact.
int tribe_terms(const GameState& s, int t) {
    const TribeState& tr = s.tribes[static_cast<std::size_t>(t)];
    int production = 0;
    int units = 0;
    int levels = 0;
    for (Slot c : tr.cities) {
        const City& city = s.cities[c];
        production += city_production(city);           // raw: no clamp, no occupation effect
        units += static_cast<int>(city.units.size());  // home-city units only
        levels += city.level;
    }
    return (5 * production) + (4 * std::popcount(tr.techs)) +
           (4 * static_cast<int>(tr.cities.size())) + (2 * units) + (3 * tr.kills) + (2 * levels);
}

// d_t (01 §4.1).
double tribe_delta(const GameState& ref, const GameState& later, int t) {
    const auto i = static_cast<std::size_t>(t);
    return (tribe_terms(later, t) - tribe_terms(ref, t)) +
           (0.1 * (later.tribes[i].score - ref.tribes[i].score));
}

int visible_tiles(const GameState& s, int player, DiffVision vision) {
    const int t = vision == DiffVision::ActiveTribe ? s.active_tribe : player;  // quirk: active's
    return static_cast<int>(s.tribes[static_cast<std::size_t>(t)].visible.count());
}

}  // namespace

double diff_heuristic(const GameState& ref, const GameState& later, int player, DiffVision vision) {
    double own = 0.0;
    double others = 0.0;
    for (int t = 0; t < ref.num_tribes; ++t) {
        const double d = tribe_delta(ref, later, t);
        (t == player ? own : others) += d;
    }
    const auto& p_ref = ref.tribes[static_cast<std::size_t>(player)];
    const auto& p_later = later.tribes[static_cast<std::size_t>(player)];
    const int connected =
        static_cast<int>(p_later.connected.size()) - static_cast<int>(p_ref.connected.size());
    const int seen = visible_tiles(later, player, vision) - visible_tiles(ref, player, vision);
    return own - (others / (ref.num_tribes - 1)) + connected + seen;
}

}  // namespace poly::search
