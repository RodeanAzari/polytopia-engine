#include "poly/observation.hpp"

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
    t.monuments = {};
    t.visible.reset();
    for (std::size_t i = 0; i < tiles; ++i) {
        t.visible.set(i);
    }
}

}  // namespace

Observation observe(const GameState& s, int observer) {
    Observation result(s, observer);
    GameState& o = result.masked_;
    const TribeState& me = s.tribes[static_cast<std::size_t>(observer)];
    const auto tiles = static_cast<std::size_t>(s.side) * static_cast<std::size_t>(s.side);
    const auto seen = [&](int x, int y) {
        return me.visible.test(static_cast<std::size_t>(s.index(x, y)));
    };

    for (std::size_t i = 0; i < tiles; ++i) {
        if (me.visible.test(i)) {
            o.resource[i] = masked(s.resource[i], me);
        } else {
            o.terrain[i] = Terrain::Fog;
            o.resource[i] = Resource::None;
            o.building[i] = Building::None;
            o.unit_at[i] = kNoSlot;
            o.city_owner[i] = kNoSlot;
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
        if (city.id == 0) {
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
    return result;
}

}  // namespace poly
