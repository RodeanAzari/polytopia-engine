#include "poly/observation.hpp"

#include <algorithm>
#include <utility>

namespace poly {

namespace {

// 04 §7: crops need Organization, ore Climbing, whales Fishing.
Resource masked(Resource r, const TribeState& t) {
    const bool hidden = (r == Resource::Crops && !t.has_tech(Tech::Organization)) ||
                        (r == Resource::Ore && !t.has_tech(Tech::Climbing)) ||
                        (r == Resource::Whales && !t.has_tech(Tech::Fishing));
    return hidden ? Resource::None : r;
}

void hide_tribe(TribeState& t, std::size_t tiles) {
    t.stars = 0;
    t.kills = 0;
    t.pacifist_count = 0;
    t.techs = 0;
    t.all_researched = false;
    t.cities.clear();
    t.tribe_units.clear();
    t.met.clear();
    t.connected.clear();
    t.monuments.fill(MonumentStatus::Absent);  // Tribes' copy: an empty map
    t.visible.reset();
    for (std::size_t i = 0; i < tiles; ++i) {
        t.visible.set(i);
    }
}

// Tribes' copy gives a fogged tile city id 0: owned (not -1), by no actor. That is how a ghost city
// behaves (tribe -1), so fogged tiles point at one phantom ghost (id kFogOwnerId, which no actor
// has; 0 would mark a free slot). It matters when rules run on the copy (04 §9): such tiles can't
// be claimed by borders or roads, and nobody meets their owner.
Slot add_fog_owner(GameState& o) {
    // Observing an observation (search does, ADR-008 §3): reuse its fog owner.
    if (auto* const own = std::ranges::find(o.cities, kFogOwnerId, &City::id);
        own != o.cities.end()) {
        return static_cast<Slot>(own - o.cities.begin());
    }
    auto* const it = std::ranges::find(o.cities, 0, &City::id);
    check(it != o.cities.end(), "observation: no free city slot for the fog owner");
    *it = City{};
    it->id = kFogOwnerId;
    it->tribe = -1;
    return static_cast<Slot>(it - o.cities.begin());
}

// Other tribes' visible actors, appended by Tribes while walking its native actor HashMap: bucket
// (id mod capacity), then insertion order, which is id order (ids only grow; none is re-inserted).
void sort_like_actor_map(GameState& o, int observer, std::int32_t capacity) {
    const auto mask = capacity - 1;
    const auto map_order = [mask](std::int32_t a, std::int32_t b) {
        return std::pair(a & mask, a) < std::pair(b & mask, b);
    };
    for (int q = 0; q < o.num_tribes; ++q) {
        if (q == observer) {
            continue;
        }
        TribeState& t = o.tribes[static_cast<std::size_t>(q)];
        std::ranges::sort(
            t.cities, [&](Slot a, Slot b) { return map_order(o.cities[a].id, o.cities[b].id); });
        std::ranges::sort(t.tribe_units,
                          [&](Slot a, Slot b) { return map_order(o.units[a].id, o.units[b].id); });
    }
}

}  // namespace

Observation observe(const GameState& s, int observer) {
    Observation result(s, observer);
    GameState& o = result.masked_;
    o.native = false;
    const TribeState& me = s.tribes[static_cast<std::size_t>(observer)];
    const auto tiles = static_cast<std::size_t>(s.side) * static_cast<std::size_t>(s.side);
    const auto seen = [&](int x, int y) {
        return me.visible.test(static_cast<std::size_t>(s.index(x, y)));
    };

    const Slot fog_owner = me.visible.count() < tiles ? add_fog_owner(o) : kNoSlot;
    for (std::size_t i = 0; i < tiles; ++i) {
        if (me.visible.test(i)) {
            o.resource[i] = masked(s.resource[i], me);
        } else {
            o.terrain[i] = Terrain::Fog;
            o.resource[i] = Resource::None;
            o.building[i] = Building::None;
            o.unit_at[i] = kNoSlot;
            o.city_owner[i] = fog_owner;
            o.network.reset(i);
        }
    }

    for (int q = 0; q < s.num_tribes; ++q) {
        if (q != observer) {
            hide_tribe(o.tribes[static_cast<std::size_t>(q)], tiles);
        }
    }

    for (std::size_t c = 0; c < o.cities.size(); ++c) {
        City& city = o.cities[c];
        if (city.id == 0 || city.id == kFogOwnerId || city.tribe < 0) {  // free, fog owner, ghost
            continue;
        }
        city.points_worth = 0;
        if (city.tribe == observer) {
            continue;
        }
        if (seen(city.x, city.y)) {
            city.population = 0;
            city.production_bonus = 0;
            city.units.clear();
            o.tribes[static_cast<std::size_t>(city.tribe)].cities.push_back(static_cast<Slot>(c));
        } else {
            const std::int32_t id = city.id;
            city = City{};
            city.id = id;
            city.tribe = -1;
        }
    }

    for (std::size_t u = 0; u < o.units.size(); ++u) {
        Unit& unit = o.units[u];
        if (unit.id == 0 || unit.tribe == observer) {
            continue;
        }
        if (seen(unit.x, unit.y)) {
            unit.city = kNoSlot;
            unit.kills = 0;
            o.tribes[static_cast<std::size_t>(unit.tribe)].tribe_units.push_back(
                static_cast<Slot>(u));
        } else {
            unit = Unit{};
        }
    }

    sort_like_actor_map(o, observer, s.actor_map_capacity);
    return result;
}

}  // namespace poly
