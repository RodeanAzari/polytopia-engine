#include "search/macro.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

namespace poly::search {

namespace {

constexpr int kFar = std::numeric_limits<int>::max();

int dist(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

// The tile of `targets` nearest to (x, y); first in list order on ties. False if the list is empty.
bool nearest(const std::vector<Tile>& targets, int x, int y, Tile& out) {
    int best = kFar;
    for (const Tile& t : targets) {
        const int d = dist(x, y, t.x, t.y);
        if (d < best) {
            best = d;
            out = t;
        }
    }
    return best != kFar;
}

// Fogged tiles a unit on (x, y) would reveal: radius 2 from a mountain, else 1 (02 §2.3, as
// clear_view on arrival). Counts only Terrain::Fog, so it is 0 under full observability.
int fog_revealed(const GameState& v, int x, int y) {
    const int r = v.terrain[static_cast<std::size_t>(v.index(x, y))] == Terrain::Mountain ? 2 : 1;
    int n = 0;
    for (int i = x - r; i <= x + r; ++i) {
        for (int j = y - r; j <= y + r; ++j) {
            n += v.on_board(i, j) &&
                         v.terrain[static_cast<std::size_t>(v.index(i, j))] == Terrain::Fog
                     ? 1
                     : 0;
        }
    }
    return n;
}

// The move (index into legal) of `moves` with the smallest key; first in legal order on ties.
template <class Key>
std::size_t best_move(std::span<const Action> legal, const std::vector<std::size_t>& moves,
                      Key key) {
    std::size_t best = moves.front();
    for (const std::size_t j : moves) {
        if (key(legal[j]) < key(legal[best])) {
            best = j;
        }
    }
    return best;
}

// Send stars (ADR-010 §2): only the largest legal amount per target tribe.
bool largest_send(std::span<const Action> legal, const Action& a) {
    return std::ranges::none_of(legal, [&](const Action& b) {
        return b.type == ActionType::SendStars && b.target == a.target && b.amount > a.amount;
    });
}

}  // namespace

// Goal targets (ADR-010 §1) from the observer's view, in list / x-major order so ties are
// deterministic. Other tribes' units on hidden tiles are not in an observation at all; `seen` is
// a second guard.
void MacroFilter::collect_targets(const Observation& obs) {
    const GameState& v = obs.view();
    const int me = obs.observer();
    const auto& vis = v.tribes[static_cast<std::size_t>(me)].visible;
    const auto seen = [&](int x, int y) {
        return vis.test(static_cast<std::size_t>(v.index(x, y)));
    };
    for (auto* t : {&enemy_units_, &enemy_cities_, &villages_, &own_cities_, &defend_}) {
        t->clear();
    }
    for (const Unit& u : v.units) {
        if (u.id != 0 && u.tribe >= 0 && u.tribe != me && seen(u.x, u.y)) {
            enemy_units_.push_back({u.x, u.y});
        }
    }
    for (const City& c : v.cities) {
        if (c.id == 0 || c.tribe < 0) {
            continue;  // free slot, ghost or fog owner
        }
        if (c.tribe == me) {
            own_cities_.push_back({c.x, c.y});
        } else if (seen(c.x, c.y)) {
            enemy_cities_.push_back({c.x, c.y});
        }
    }
    for (int x = 0; x < v.side; ++x) {
        for (int y = 0; y < v.side; ++y) {
            if (seen(x, y) &&
                v.terrain[static_cast<std::size_t>(v.index(x, y))] == Terrain::Village) {
                villages_.push_back({x, y});
            }
        }
    }
    // Defend: the own city nearest to a visible enemy unit (one target for every unit).
    int best = kFar;
    Tile pick;
    for (const Tile& c : own_cities_) {
        Tile e;
        if (nearest(enemy_units_, c.x, c.y, e) && dist(c.x, c.y, e.x, e.y) < best) {
            best = dist(c.x, c.y, e.x, e.y);
            pick = c;
        }
    }
    if (best != kFar) {
        defend_.push_back(pick);
    }
}

// ADR-010 §1: the goal moves of the unit that makes legal[first] (its first move); marks all its
// moves done.
void MacroFilter::keep_unit_moves(const GameState& v, std::span<const Action> legal,
                                  std::span<const int> rb_scores, std::size_t first) {
    const std::int32_t actor = legal[first].actor;
    moves_.clear();
    for (std::size_t j = first; j < legal.size(); ++j) {
        if (legal[j].type == ActionType::Move && legal[j].actor == actor) {
            moves_.push_back(j);
            done_[j] = true;
        }
    }
    const auto* const it = std::ranges::find(v.units, actor, &Unit::id);
    if (it == v.units.end()) {  // not in the observation: keep its moves unchanged
        for (const std::size_t j : moves_) {
            kept_[j] = true;
        }
        return;
    }
    const Unit& u = *it;
    const auto toward = [&](const std::vector<Tile>& targets) {
        Tile t;
        if (!nearest(targets, u.x, u.y, t)) {
            return;
        }
        const std::size_t best = best_move(legal, moves_, [&](const Action& m) {
            return std::pair(dist(m.x, m.y, t.x, t.y), dist(m.x, m.y, u.x, u.y));
        });
        if (dist(legal[best].x, legal[best].y, t.x, t.y) < dist(u.x, u.y, t.x, t.y)) {
            kept_[best] = true;  // only a move that gets closer
        }
    };
    toward(enemy_units_);   // attack
    toward(enemy_cities_);  // siege
    toward(villages_);      // capture
    toward(defend_);        // defend
    // Explore: the most fog revealed, nearest the unit on ties; only if it reveals any.
    const std::size_t explore = best_move(legal, moves_, [&](const Action& m) {
        return std::pair(-fog_revealed(v, m.x, m.y), dist(m.x, m.y, u.x, u.y));
    });
    if (fog_revealed(v, legal[explore].x, legal[explore].y) > 0) {
        kept_[explore] = true;
    }
    // Rule Based's pick: the highest score.
    kept_[best_move(legal, moves_, [&](const Action& m) {
        return -rb_scores[static_cast<std::size_t>(&m - legal.data())];
    })] = true;
}

void MacroFilter::select(const Observation& obs, std::span<const Action> legal,
                         std::span<const int> rb_scores, std::vector<std::size_t>& keep) {
    collect_targets(obs);
    kept_.assign(legal.size(), false);
    done_.assign(legal.size(), false);
    for (std::size_t i = 0; i < legal.size(); ++i) {
        switch (legal[i].type) {
            case ActionType::Destroy:
            case ActionType::Disband:
                break;  // ADR-010 §2: Rule Based scores −2 and never plays them
            case ActionType::SendStars:
                kept_[i] = largest_send(legal, legal[i]);
                break;
            case ActionType::Move:
                if (!done_[i]) {
                    keep_unit_moves(obs.view(), legal, rb_scores, i);
                }
                break;
            default:
                kept_[i] = true;
        }
    }
    keep.clear();
    bool other = false;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        if (kept_[i]) {
            keep.push_back(i);
            other = other || legal[i].type != ActionType::EndTurn;
        }
    }
    if (!other && legal.size() > keep.size()) {  // ADR-010 §5: never reduce to End Turn alone
        keep.resize(legal.size());
        for (std::size_t i = 0; i < legal.size(); ++i) {
            keep[i] = i;
        }
    }
}

}  // namespace poly::search
