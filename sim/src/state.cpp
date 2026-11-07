#include "poly/state.hpp"

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
// accepts p itself; the meeting is one-directional).
void meet(GameState& s, int p, int q) {
    auto& met = s.tribes[static_cast<std::size_t>(p)].met;
    if (!met.empty() && (q == p || met.contains(static_cast<std::int8_t>(q)))) {
        return;
    }
    met.push_back(static_cast<std::int8_t>(q));
    // Partial obs only: meeting tech draw and Eye of God unlock (not reachable yet, see
    // clear_view).
}

void view_tile(GameState& s, int p, int x, int y) {
    const int i = s.index(x, y);
    auto& t = s.tribes[static_cast<std::size_t>(p)];
    if (!t.visible.test(static_cast<std::size_t>(i))) {
        t.visible.set(static_cast<std::size_t>(i));
        t.score += kRevealTilePoints;
        // Reveal-triggered network recomputation: partial obs only (not reachable yet).
    }
    const Slot u = s.unit_at[static_cast<std::size_t>(i)];
    if (u != kNoSlot) {
        meet(s, p, s.units[u].tribe);
    }
    const Slot c = s.city_owner[static_cast<std::size_t>(i)];
    if (c != kNoSlot) {
        meet(s, p, s.cities[c].tribe);
    }
}

}  // namespace

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

void clear_view(GameState& s, int tribe, int x, int y, int radius) {
    check(s.obs == ObsMode::Full,
          "partial-obs clear view needs the Chance interface (not built yet)");
    for_each_neighbour(s, x, y, radius, [&](int nx, int ny) { view_tile(s, tribe, nx, ny); });
    view_tile(s, tribe, x, y);
}

}  // namespace poly
