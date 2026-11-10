#include "search/rule_based.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>
#include <vector>

#include "poly/rules.hpp"

namespace poly::search {

namespace {

// Sheet 05 conventions, all on the observation `v` of tribe `me`.
struct View {
    const GameState* state;
    int me;

    [[nodiscard]] const GameState& v() const { return *state; }
    [[nodiscard]] const TribeState& mine() const {
        return v().tribes[static_cast<std::size_t>(me)];
    }
    [[nodiscard]] int stars() const { return mine().stars; }
    [[nodiscard]] int production() const {
        int sum = 0;
        for (const Slot c : mine().cities) {
            sum += city_production(v().cities[c]);
        }
        return sum;
    }
    [[nodiscard]] bool visible(int x, int y) const {
        return mine().visible.test(static_cast<std::size_t>(v().index(x, y)));
    }
    [[nodiscard]] const Unit* unit_at(int x, int y) const {
        const Slot u = v().unit_at[static_cast<std::size_t>(v().index(x, y))];
        return u == kNoSlot ? nullptr : &v().units[u];
    }
    // Tribes' getCityInBorders: the tile's owner city, if it is an actor of the copy (ghosts and
    // the fog owner, tribe -1, are not).
    [[nodiscard]] const City* city_in_borders(int x, int y) const {
        const Slot c = v().city_owner[static_cast<std::size_t>(v().index(x, y))];
        return c == kNoSlot || v().cities[c].tribe < 0 ? nullptr : &v().cities[c];
    }
    [[nodiscard]] const Unit* unit_by_id(std::int32_t id) const {
        const auto* const it = std::ranges::find(v().units, id, &Unit::id);
        return id == 0 || it == v().units.end() ? nullptr : &*it;
    }
    [[nodiscard]] const City* city_by_id(std::int32_t id) const {
        const auto* const it = std::ranges::find(v().cities, id, &City::id);
        return id == 0 || it == v().cities.end() ? nullptr : &*it;
    }
    [[nodiscard]] bool is_road(int x, int y) const {  // Board.isRoad
        const auto i = static_cast<std::size_t>(v().index(x, y));
        const Terrain t = v().terrain[i];
        return v().network.test(i) && t != Terrain::ShallowWater && t != Terrain::DeepWater &&
               t != Terrain::City;
    }
    [[nodiscard]] int allegiance(int a, int b) const {
        return v().allegiance[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)];
    }
    // Ring(p, r): tiles within Chebyshev r of p, p excluded, x-major then y; f returns true to
    // stop.
    template <typename F>
    [[nodiscard]] bool any_in_ring(int px, int py, int r, const F& f) const {
        for (int x = px - r; x <= px + r; ++x) {
            for (int y = py - r; y <= py + r; ++y) {
                if ((x != px || y != py) && v().on_board(x, y) && f(x, y)) {
                    return true;
                }
            }
        }
        return false;
    }
    // Board.getCityTiles: tiles of city c within radius 1 (level < 4) or 2, x-major.
    template <typename F>
    void city_tiles(const City& c, const F& f) const {
        const int r = c.level < 4 ? 1 : 2;
        const auto slot = static_cast<Slot>(&c - v().cities.data());
        for (int x = c.x - r; x <= c.x + r; ++x) {
            for (int y = c.y - r; y <= c.y + r; ++y) {
                if (v().on_board(x, y) &&
                    v().city_owner[static_cast<std::size_t>(v().index(x, y))] == slot) {
                    f(x, y);
                }
            }
        }
    }
};

int dist(int x0, int y0, int x1, int y1) { return std::max(std::abs(x0 - x1), std::abs(y0 - y1)); }
int atk(const Unit& u) { return kUnitAttack[static_cast<std::size_t>(u.type)]; }
int def(const Unit& u) { return kUnitDefence[static_cast<std::size_t>(u.type)]; }
int range(const Unit& u) { return kUnitRange[static_cast<std::size_t>(u.type)]; }
bool ranged(UnitType t) {
    return t == UnitType::Boat || t == UnitType::Ship || t == UnitType::Battleship ||
           t == UnitType::Archer || t == UnitType::Catapult;
}
int needed(const City& c) { return c.level + 1 - city_production(c); }
int clamp5(int v) { return std::clamp(v, 0, 5); }
// h(x, y, z) of sheet 05: by allegiance band.
int band(int a, int hostile, int low, int high) {
    if (a < 0) {
        return hostile;
    }
    return a < 15 ? low : high;
}

// Move stage 1: the first visible foreign unit (x-major) that the move approaches while it is
// weaker, or moves away from while it is stronger and in range; 0 if none.
int move_vs_units(const View& w, const Unit& u, int mx, int my) {
    for (int x = 0; x < w.v().side; ++x) {
        for (int y = 0; y < w.v().side; ++y) {
            const Unit* e = w.visible(x, y) ? w.unit_at(x, y) : nullptr;
            if (e == nullptr || e->tribe == w.me) {
                continue;
            }
            const bool in_range = dist(e->x, e->y, u.x, u.y) <= range(*e);
            const int before = dist(u.x, u.y, e->x, e->y);
            const int after = dist(mx, my, e->x, e->y);
            const bool weaker = def(*e) < atk(u) && u.hp >= e->hp;
            if (weaker && after < before) {
                return 3;
            }
            if (!weaker && after > before && in_range) {
                return 4;
            }
        }
    }
    return 0;
}

// Move stage 2: visible tiles around the unit (its range): toward a foreign city's borders 4,
// toward a village 5 (city checked first per tile); 0 if none.
int move_vs_places(const View& w, const Unit& u, int mx, int my) {
    int found = 0;
    (void)w.any_in_ring(u.x, u.y, range(u), [&](int x, int y) {
        if (!w.visible(x, y)) {
            return false;
        }
        const City* c = w.city_in_borders(x, y);
        if (c != nullptr && c->tribe != w.me &&
            dist(mx, my, c->x, c->y) < dist(u.x, u.y, c->x, c->y)) {
            found = 4;
        } else if (w.v().terrain[static_cast<std::size_t>(w.v().index(x, y))] == Terrain::Village &&
                   dist(mx, my, x, y) < dist(u.x, u.y, x, y)) {
            found = 5;
        }
        return found != 0;
    });
    return found;
}

int score_move(const View& w, const Action& a) {
    const Unit* u = w.unit_by_id(a.actor);
    if (u == nullptr) {
        return 0;
    }
    if (const int s1 = move_vs_units(w, *u, a.x, a.y); s1 != 0) {
        return s1;
    }
    if (const int s2 = move_vs_places(w, *u, a.x, a.y); s2 != 0) {
        return s2;
    }
    return w.any_in_ring(a.x, a.y, 1, [&](int x, int y) { return w.visible(x, y); }) ? 3 : 0;
}

int score_attack(const View& w, const Action& a) {
    const Unit* att = w.unit_by_id(a.actor);
    const Unit* d = w.unit_by_id(a.target);
    if (att == nullptr || d == nullptr) {
        return 0;
    }
    const int al = w.allegiance(att->tribe, d->tribe);
    if (!ranged(att->type)) {
        if (att->hp >= d->hp) {
            return atk(*att) > def(*d) ? band(al, 5, 2, 0) : band(al, 1, 0, 0);
        }
        return atk(*att) > def(*d) ? band(al, 1, 0, 0) : 0;
    }
    const int dd = dist(att->x, att->y, d->x, d->y);
    const bool in_their_range = dd <= range(*d);
    const bool in_my_range = dd <= range(*att);
    if (in_their_range && atk(*d) > def(*att)) {
        return 0;
    }
    if (in_my_range && !in_their_range) {
        return band(al, 5, 2, 0);
    }
    if (def(*d) < atk(*att) && att->hp >= d->hp) {
        return band(al, 4, 1, 0);
    }
    return 0;
}

int score_recover(const View& w, const Action& a) {
    const Unit* u = w.unit_by_id(a.actor);
    if (u == nullptr) {
        return 0;
    }
    for (int x = 0; x < w.v().side; ++x) {
        for (int y = 0; y < w.v().side; ++y) {
            const Unit* e = w.visible(x, y) ? w.unit_at(x, y) : nullptr;
            if (e != nullptr && e->tribe != w.me && dist(e->x, e->y, u->x, u->y) <= range(*e)) {
                return e->hp < u->hp ? 3 : 1;
            }
        }
    }
    return 4;
}

int score_heal(const View& w, const Action& a) {
    const Slot healer = unit_by_id(w.v(), a.actor);
    const Unit& h = w.v().units[healer];
    HealTargets targets;
    heal_targets(w.v(), healer, targets);
    int count = 0;
    for (const Slot ts : targets) {
        const Unit& t = w.v().units[ts];
        (void)w.any_in_ring(h.x, h.y, range(t), [&](int x, int y) {  // quirk: target's range
            const Unit* e = w.unit_at(x, y);
            if (e != nullptr && e->tribe != t.tribe && e->hp > t.hp) {
                ++count;
            }
            return false;
        });
    }
    return std::min(count, 5);
}

int score_convert(const View& w, const Action& a, long long& convert_scored) {
    ++convert_scored;
    const Unit* u = w.unit_by_id(a.actor);
    const Unit* t = w.unit_by_id(a.target);
    if (u == nullptr || t == nullptr) {
        return 0;
    }
    // Tribes indexes the allegiance matrix with unit ids here (it would throw); we use the tribes.
    const int al = w.allegiance(u->tribe, t->tribe);
    switch (t->type) {
        case UnitType::Battleship:
        case UnitType::SuperUnit:
        case UnitType::Swordsman:
        case UnitType::Knight:
        case UnitType::Catapult:
            return band(al, 5, 2, 0);
        case UnitType::MindBender:
        case UnitType::Boat:
        case UnitType::Ship:
        case UnitType::Warrior:
            return band(al, 4, 1, 0);
        default:
            return 0;
    }
}

int score_research(Tech t) {
    switch (t) {
        case Tech::Organization:
        case Tech::Climbing:
        case Tech::Mining:
        case Tech::Fishing:
        case Tech::Hunting:
        case Tech::Whaling:
            return 5;
        case Tech::Farming:
        case Tech::Aquatism:
        case Tech::Riding:
        case Tech::Forestry:
        case Tech::Archery:
        case Tech::Sailing:
        case Tech::Shields:
        case Tech::Chivalry:
            return 4;
        default:
            return 3;
    }
}

int score_spawn(const View& w, const Action& a) {
    const City* c = w.city_by_id(a.actor);
    int enemies = 0;
    w.city_tiles(*c, [&](int x, int y) {
        const Unit* e = w.visible(x, y) ? w.unit_at(x, y) : nullptr;
        enemies += e != nullptr && e->tribe != w.me ? 1 : 0;
    });
    const bool threat = enemies > 0;
    switch (static_cast<UnitType>(a.param)) {
        case UnitType::Rider:
        case UnitType::Archer:
        case UnitType::Catapult:
            return threat ? 0 : 3;
        case UnitType::Swordsman:
        case UnitType::Knight:
            return threat ? 4 : 3;
        case UnitType::MindBender:
            return threat ? 0 : 2;
        case UnitType::Warrior:
            return 3;
        case UnitType::Defender:
            return threat ? 5 : 3;
        default:
            return 0;
    }
}

int score_road(const View& w, const Action& a) {
    const bool rich = w.production() > 5 && w.stars() > 4;
    int found = 0;
    (void)w.any_in_ring(a.x, a.y, 1, [&](int x, int y) {
        const City* c = w.city_in_borders(x, y);
        if (c != nullptr && c->tribe == w.me) {
            if (std::cmp_equal(x, c->x) && std::cmp_equal(y, c->y)) {
                found = rich ? 3 : 0;  // a centre without the economy doesn't match
            } else {
                found = w.is_road(x, y) ? 3 : 0;
            }
        } else if (w.is_road(x, y)) {
            found = rich ? 4 : 3;
        }
        return found != 0;
    });
    return found;
}

int score_send_stars(const View& w, const Action& a) {
    const int s = w.stars();
    const int al = w.allegiance(w.me, a.target);
    if (s > 60) {
        return 4;
    }
    if (s > 30) {
        return al < 0 && al + a.amount > 0 ? 3 : 2;
    }
    return -5 <= al && al < 0 && a.amount <= 5 ? 4 : -1;
}

int score_city_action(const View& w, const Action& a) {
    const City* c = w.city_by_id(a.actor);
    switch (a.type) {
        case ActionType::GatherResource:
            return clamp5(5 - needed(*c));
        case ActionType::LevelUp: {
            const auto b = static_cast<LevelUpBonus>(a.param);
            return b == LevelUpBonus::BorderGrowth || b == LevelUpBonus::Workshop ||
                           b == LevelUpBonus::Resources || b == LevelUpBonus::SuperUnit
                       ? 5
                       : 0;
        }
        case ActionType::Build: {
            if (w.stars() < 8) {
                return 0;
            }
            // Quirk: every listed building falls through to 1; Forest temple isn't listed.
            const int base = static_cast<Building>(a.param) == Building::ForestTemple ? 0 : 1;
            return clamp5(base + 5 - needed(*c));
        }
        case ActionType::Spawn:
            return score_spawn(w, a);
        case ActionType::ClearForest:
            return w.stars() == 0 ? 3 : 0;
        case ActionType::BurnForest: {
            if (w.stars() <= 5) {
                return 0;
            }
            const auto farms =
                std::ranges::count(c->buildings, Building::Farm, &BuildingEntry::type);
            return std::min(5, 1 + (farms < 2 ? static_cast<int>(farms) : 0));
        }
        case ActionType::GrowForest: {
            int forests = 0;
            int huts = 0;
            w.city_tiles(*c, [&](int x, int y) {
                const auto i = static_cast<std::size_t>(w.v().index(x, y));
                forests += w.v().terrain[i] == Terrain::Forest ? 1 : 0;
                huts += w.v().building[i] == Building::LumberHut ? 1 : 0;
            });
            return forests < 2 && huts < 2 ? 4 : 2;
        }
        case ActionType::Destroy:
            return -2;
        default:
            return 0;
    }
}

}  // namespace

int rule_based_score(const Observation& obs, const Action& a, long long& convert_scored) {
    const View w{.state = &obs.view(), .me = obs.observer()};
    switch (a.type) {
        case ActionType::Move:
            return score_move(w, a);
        case ActionType::Attack:
            return score_attack(w, a);
        case ActionType::Upgrade:
            return w.production() > 3 && w.stars() > 6 ? 3
                                                       : 0;  // the battleship branch never applies
        case ActionType::Recover:
            return score_recover(w, a);
        case ActionType::Capture:
        case ActionType::Examine:
        case ActionType::MakeVeteran:
            return 5;
        case ActionType::HealOthers:
            return score_heal(w, a);
        case ActionType::Convert:
            return score_convert(w, a, convert_scored);
        case ActionType::Disband:
            return -2;
        case ActionType::BuildRoad:
            return score_road(w, a);
        case ActionType::ResearchTech:
            return score_research(static_cast<Tech>(a.param));
        case ActionType::SendStars:
            return score_send_stars(w, a);
        case ActionType::EndTurn:
            return -1;
        case ActionType::DeclareWar:
            return 0;
        default:
            return score_city_action(w, a);
    }
}

Action RuleBasedAgent::act(const Observation& obs, std::span<const Action> legal) {
    std::vector<int> scores(legal.size());
    int best = -1;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        scores[i] = rule_based_score(obs, legal[i], convert_scored_);
        best = std::max(best, scores[i]);
    }
    std::vector<std::size_t> top;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        if (scores[i] == best) {
            top.push_back(i);
        }
    }
    check(!top.empty(), "rule based: nothing scores -1 or more (End turn always does)");
    return legal[top[rng_.below(static_cast<std::uint32_t>(top.size()))]];
}

}  // namespace poly::search
