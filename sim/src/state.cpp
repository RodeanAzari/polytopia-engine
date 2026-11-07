#include "poly/state.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace poly {

namespace {

// Neighbourhood of radius r around (x, y): the (2r+1)^2 square minus the centre, clipped to the
// board, x-major then y, ascending (rule facts 02, conventions).
template <typename F>
void for_each_neighbour(const GameState& s, int x, int y, int r, const F& f) {
    for (int nx = x - r; nx <= x + r; ++nx) {
        for (int ny = y - r; ny <= y + r; ++ny) {
            if ((nx != x || ny != y) && s.on_board(nx, ny)) {
                f(nx, ny);
            }
        }
    }
}

// Meet tribe q, seen by tribe p (03b §1 "Meet tribe Q", including its quirks: an empty met-list
// accepts p itself; the meeting is one-directional). Partial obs: p draws one tech q has and p
// lacks; [Tribes quirk] it is granted only if researchable (parent known), the draw happens anyway.
void meet(GameState& s, int p, int q, Chance& chance) {
    auto& t = s.tribes[static_cast<std::size_t>(p)];
    if (!t.met.empty() && (q == p || t.met.contains(static_cast<std::int8_t>(q)))) {
        return;
    }
    t.met.push_back(static_cast<std::int8_t>(q));
    if (s.obs != ObsMode::Partial) {
        return;
    }
    const auto& other = s.tribes[static_cast<std::size_t>(q)];
    std::array<Tech, kNumTechs> candidates{};
    int n = 0;
    for (int k = 0; k < kNumTechs; ++k) {
        const auto tech = static_cast<Tech>(k);
        if (other.has_tech(tech) && !t.has_tech(tech)) {
            candidates[static_cast<std::size_t>(n++)] = tech;
        }
    }
    if (n > 0) {
        const Tech tech = candidates[static_cast<std::size_t>(chance.meeting_tech(n))];
        if (researchable(t, tech)) {
            mark_researched(t, tech);
        }
    }
}

// Returns whether a road or water tile was newly revealed.
bool view_tile(GameState& s, int p, int x, int y, Chance& chance) {
    const auto i = static_cast<std::size_t>(s.index(x, y));
    auto& t = s.tribes[static_cast<std::size_t>(p)];
    bool network = false;
    if (!t.visible.test(i)) {
        t.visible.set(i);
        t.score += kRevealTilePoints;
        const Terrain ter = s.terrain[i];
        const bool water = ter == Terrain::ShallowWater || ter == Terrain::DeepWater;
        const bool road = s.network.test(i) && !water && ter != Terrain::City;
        network = water || road;
    }
    if (const Slot u = s.unit_at[i]; u != kNoSlot) {
        meet(s, p, s.units[u].tribe, chance);
    }
    if (const Slot c = s.city_owner[i]; c != kNoSlot) {
        meet(s, p, s.cities[c].tribe, chance);
    }
    return network;
}

}  // namespace

bool researchable(const TribeState& t, Tech tech) {
    const auto parent = kTechParent[static_cast<std::size_t>(tech)];
    return !t.has_tech(tech) && (!parent || t.has_tech(*parent));
}

void mark_researched(TribeState& t, Tech tech) {
    t.techs |= 1U << static_cast<unsigned>(tech);
    const bool leaf =
        std::ranges::none_of(kTechParent, [&](const std::optional<Tech>& p) { return p == tech; });
    if (leaf) {
        t.all_researched = t.techs == (1U << kNumTechs) - 1;
    }
}

void add_population(GameState& s, City& c, int p, int v) {
    if (c.population + v < -c.level) {
        v = -c.level - c.population;
    }
    c.population = static_cast<std::int16_t>(c.population + v);
    s.tribes[static_cast<std::size_t>(p)].score += kPopulationPoints * v;
    c.points_worth += kPopulationPoints * v;
}

Slot new_city(GameState& s) {
    for (std::size_t i = 0; i < s.cities.size(); ++i) {
        if (s.cities[i].id == 0) {
            s.cities[i] = City{};
            s.cities[i].id = s.next_actor_id++;
            return static_cast<Slot>(i);
        }
    }
    check(false, "city pool exhausted (kMaxCities)");
    return kNoSlot;
}

Slot new_unit(GameState& s) {
    for (std::size_t i = 0; i < s.units.size(); ++i) {
        if (s.units[i].id == 0) {
            s.units[i] = Unit{};
            s.units[i].id = s.next_actor_id++;
            return static_cast<Slot>(i);
        }
    }
    check(false, "unit pool exhausted (kMaxUnits)");
    return kNoSlot;
}

void claim_tiles(GameState& s, Slot city, int radius) {
    City& c = s.cities[city];
    const int tribe = c.tribe;
    auto& t = s.tribes[static_cast<std::size_t>(tribe)];
    auto claim = [&](int x, int y) {
        Slot& owner = s.city_owner[static_cast<std::size_t>(s.index(x, y))];
        if (owner == kNoSlot) {
            owner = city;
            t.score += kBorderTilePoints;
            c.points_worth += kBorderTilePoints;
        }
    };
    for_each_neighbour(s, c.x, c.y, radius, claim);
    claim(c.x, c.y);
}

bool clear_view(GameState& s, int tribe, int x, int y, int radius, Chance& chance) {
    bool network = false;
    for_each_neighbour(s, x, y, radius, [&](int nx, int ny) {
        network = view_tile(s, tribe, nx, ny, chance) || network;
    });
    network = view_tile(s, tribe, x, y, chance) || network;
    auto& t = s.tribes[static_cast<std::size_t>(tribe)];
    // 03a §5: Eye of God once every tile is visible (partial obs only).
    if (s.obs == ObsMode::Partial &&
        monument(t, Building::EyeOfGod) == MonumentStatus::Unavailable &&
        std::cmp_equal(t.visible.count(), s.side * s.side)) {
        monument(t, Building::EyeOfGod) = MonumentStatus::Available;
    }
    return network;
}

}  // namespace poly
