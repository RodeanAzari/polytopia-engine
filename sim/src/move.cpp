#include "poly/move.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace poly {

namespace {

bool is_water(Terrain t) { return t == Terrain::ShallowWater || t == Terrain::DeepWater; }

// Road tile (02 §3): in the road layer, not water, not a city.
bool is_road(const GameState& s, std::size_t i) {
    return s.network.test(i) && !is_water(s.terrain[i]) && s.terrain[i] != Terrain::City;
}

bool traversable(const GameState& s, std::size_t i, const TribeState& t) {
    switch (s.terrain[i]) {
        case Terrain::Mountain:
            return t.has_tech(Tech::Climbing);
        case Terrain::ShallowWater:
            return t.has_tech(Tech::Sailing);
        case Terrain::DeepWater:
            return t.has_tech(Tech::Navigation);
        default:
            return true;
    }
}

// Inside no city's borders, or inside one of the mover's tribe's cities.
bool own_or_free(const GameState& s, std::size_t i, int tribe) {
    const Slot c = s.city_owner[i];
    return c == kNoSlot || s.cities[c].tribe == tribe;
}

// A binary min-heap of tile indices with java.util.PriorityQueue's sift rules. Keys are read live
// from `cost`, so lowering a queued tile's cost changes later comparisons without re-heapifying,
// as in Tribes.
class JavaHeap {
   public:
    explicit JavaHeap(const std::array<double, kMaxTiles>& cost) : cost_(&cost) {}

    [[nodiscard]] bool empty() const { return size_ == 0; }

    void push(std::uint16_t v) {
        check(std::cmp_less(size_, q_.size()), "move heap overflow");
        int k = size_++;
        while (k > 0) {
            const int parent = (k - 1) / 2;
            if (key(v) >= key(q_[parent])) {
                break;
            }
            q_[k] = q_[parent];
            k = parent;
        }
        q_[k] = v;
    }

    std::uint16_t pop() {
        const std::uint16_t top = q_[0];
        const std::uint16_t x = q_[--size_];
        if (size_ > 0) {
            int k = 0;
            const int half = size_ / 2;
            while (k < half) {
                int child = (2 * k) + 1;
                const int right = child + 1;
                if (right < size_ && key(q_[child]) > key(q_[right])) {
                    child = right;
                }
                if (key(x) <= key(q_[child])) {
                    break;
                }
                q_[k] = q_[child];
                k = child;
            }
            q_[k] = x;
        }
        return top;
    }

   private:
    [[nodiscard]] double key(std::uint16_t v) const { return (*cost_)[v]; }

    const std::array<double, kMaxTiles>* cost_;
    std::array<std::uint16_t, kMaxTiles> q_{};
    int size_ = 0;
};

bool other_unit(const GameState& s, std::size_t i, int tribe) {
    const Slot o = s.unit_at[i];
    return o != kNoSlot && s.units[o].tribe != tribe;
}

// Zone of control (02 §3): another tribe's unit on a neighbour of (x, y).
bool in_zoc(const GameState& s, int x, int y, int tribe) {
    for (int ax = x - 1; ax <= x + 1; ++ax) {
        for (int ay = y - 1; ay <= y + 1; ++ay) {
            if ((ax != x || ay != y) && s.on_board(ax, ay) &&
                other_unit(s, static_cast<std::size_t>(s.index(ax, ay)), tribe)) {
                return true;
            }
        }
    }
    return false;
}

// Whether unit may step onto tile n at all (02 §3), before costs.
bool enterable(const GameState& s, const Unit& unit, const TribeState& tribe, std::size_t n) {
    if (other_unit(s, n, unit.tribe) || !tribe.visible.test(n) || !traversable(s, n, tribe)) {
        return false;
    }
    return unit.type != UnitType::MindBender || s.terrain[n] != Terrain::City ||
           s.cities[s.city_owner[n]].tribe == unit.tribe;
}

// Cost of stepping onto n without zone of control (02 §3); negative when a land unit meets water
// without a port. `rest` is the allowance left, `on_road` whether the step starts on a usable road.
double step_cost(const GameState& s, bool naval, std::size_t n, double rest, bool on_road) {
    const Terrain t = s.terrain[n];
    if (naval) {
        return is_water(t) ? 1.0 : rest;
    }
    double step = 0;
    if (is_water(t)) {
        if (s.building[n] != Building::Port) {
            return -1;
        }
        step = rest;
    } else {
        step = (t == Terrain::Forest || t == Terrain::Mountain) ? rest : 1.0;
    }
    if (on_road && (is_road(s, n) || t == Terrain::City)) {
        step = std::max(0.5, step / 2.0);
    }
    return step;
}

// A step from tile i can use roads: i is a road or city inside no enemy borders (02 §3).
bool road_start(const GameState& s, std::size_t i, int tribe) {
    return (is_road(s, i) || s.terrain[i] == Terrain::City) && own_or_free(s, i, tribe);
}

// Cost after stepping from a tile reached at cost c onto (nx, ny), or -1 when the step is not
// allowed (02 §3). `on_road`: the step starts on a road or city usable by the mover.
double step_to(const GameState& s, const Unit& unit, const TribeState& tribe, int nx, int ny,
               double c, bool on_road) {
    const double mov = kUnitMove[static_cast<std::size_t>(unit.type)];
    const auto n = static_cast<std::size_t>(s.index(nx, ny));
    if (!enterable(s, unit, tribe, n)) {
        return -1;
    }
    const double rest = c < mov ? mov - c : mov;
    double step = step_cost(s, is_naval(unit.type), n, rest, on_road);
    if (step < 0) {
        return -1;
    }
    if (in_zoc(s, nx, ny, unit.tribe)) {
        step = rest;
        return c + step > mov ? -1 : c + step;
    }
    return std::floor(c + step) > mov ? -1 : c + step;
}

// Generation keeps only free destinations (02 §3).
void keep_free(const GameState& s, MoveTargets& out) {
    int kept = 0;
    for (int i = 0; i < out.count; ++i) {
        const std::uint16_t t = out.tiles[static_cast<std::size_t>(i)];
        if (s.unit_at[t] == kNoSlot) {
            out.tiles[static_cast<std::size_t>(kept++)] = t;
        }
    }
    out.count = kept;
}

}  // namespace

bool can_attack(const Unit& u) {
    switch (u.type) {
        case UnitType::MindBender:
        case UnitType::Catapult:
        case UnitType::Defender:
            return u.status == TurnStatus::Fresh;
        case UnitType::Knight:
            return u.status == TurnStatus::Fresh || u.status == TurnStatus::Moved ||
                   u.status == TurnStatus::Attacked;
        default:
            return u.status == TurnStatus::Fresh || u.status == TurnStatus::Moved;
    }
}

TurnStatus status_after_attack(const Unit& u) {
    if (u.type == UnitType::Rider) {
        return u.status == TurnStatus::Fresh ? TurnStatus::Attacked : TurnStatus::MovedAndAttacked;
    }
    return TurnStatus::Finished;
}

bool can_move(const Unit& u) {
    switch (u.type) {
        case UnitType::Rider:
            return u.status == TurnStatus::Fresh || u.status == TurnStatus::Attacked ||
                   u.status == TurnStatus::MovedAndAttacked;
        default:
            return u.status == TurnStatus::Fresh;
    }
}

TurnStatus status_after_move(const Unit& u) {
    switch (u.type) {
        case UnitType::MindBender:
        case UnitType::Catapult:
        case UnitType::Defender:
        case UnitType::SuperUnit:
            return TurnStatus::Finished;
        case UnitType::Rider:
            // From Attacked, Tribes' if-chain without else passes through Moved-and-attacked to
            // Finished (02 §1 quirk).
            return u.status == TurnStatus::Fresh ? TurnStatus::Moved : TurnStatus::Finished;
        default:
            return TurnStatus::Moved;
    }
}

void move_targets(const GameState& s, Slot u, MoveTargets& out) {
    out.count = 0;
    const Unit& unit = s.units[u];
    const TribeState& tribe = s.tribes[static_cast<std::size_t>(unit.tribe)];
    const double mov = kUnitMove[static_cast<std::size_t>(unit.type)];
    const int side = s.side;

    std::array<double, kMaxTiles> cost{};
    std::bitset<kMaxTiles> seen;
    JavaHeap open(cost);
    const auto root = static_cast<std::uint16_t>(s.index(unit.x, unit.y));
    seen.set(root);
    open.push(root);

    while (!open.empty()) {
        const std::uint16_t from = open.pop();
        if (from != root) {
            out.tiles[static_cast<std::size_t>(out.count++)] = from;
        }
        const double c = cost[from];
        if (c == mov) {
            continue;  // 02 §3: no outgoing steps at exactly the movement allowance
        }
        const int fx = from / side;
        const int fy = from % side;
        const bool on_road = road_start(s, from, unit.tribe);
        for (int nx = fx - 1; nx <= fx + 1; ++nx) {
            for (int ny = fy - 1; ny <= fy + 1; ++ny) {
                if ((nx == fx && ny == fy) || !s.on_board(nx, ny)) {
                    continue;
                }
                const double next = step_to(s, unit, tribe, nx, ny, c, on_road);
                if (next < 0) {
                    continue;
                }
                const auto n = static_cast<std::size_t>(s.index(nx, ny));
                if (!seen.test(n)) {
                    seen.set(n);
                    cost[n] = next;
                    open.push(static_cast<std::uint16_t>(n));
                } else if (next < cost[n]) {
                    cost[n] = next;  // in place, no re-heapify (Tribes quirk)
                }
            }
        }
    }
    keep_free(s, out);
}

}  // namespace poly
