#include "poly/rules.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <span>
#include <utility>

#include "poly/chance.hpp"
#include "poly/move.hpp"
#include "poly/network.hpp"

namespace poly {

namespace {

constexpr std::array<int, 5> kTemplePoints = {100, 50, 50, 50, 150};  // 01, per temple level
constexpr int kTempleTurns = 3;
constexpr int kTempleMaxLevel = 5;

TribeState& tribe(GameState& s, int p) { return s.tribes[static_cast<std::size_t>(p)]; }
const TribeState& tribe(const GameState& s, int p) { return s.tribes[static_cast<std::size_t>(p)]; }

// Adds stars; Emperor's Tomb unlocks when the total reaches the threshold (03a §5).
void add_stars(TribeState& t, int n) {
    t.stars += n;
    if (t.stars >= kTombStars) {
        unlock(t, Building::EmperorsTomb);
    }
}

int production(const City& c) {
    if (c.population < 0) {
        return c.population;  // 03a §1 quirk: negative population is negative income
    }
    return c.level + c.production_bonus + (c.capital ? 1 : 0);
}

int chebyshev(int x0, int y0, int x1, int y1) {
    return std::max(std::abs(x0 - x1), std::abs(y0 - y1));
}

// 03b §2: tiles a city's actions may use.
bool is_city_tile(const GameState& s, Slot c, int x, int y) {
    const City& city = s.cities[c];
    return s.on_board(x, y) && s.city_owner[static_cast<std::size_t>(s.index(x, y))] == c &&
           chebyshev(city.x, city.y, x, y) <= (city.level < 4 ? 1 : 2);
}

// 03a §3 temple tick; returns the points scored.
int tick_temple(BuildingEntry& b) {
    if (b.temple_level >= kTempleMaxLevel) {
        return 0;
    }
    if (--b.turns_to_score > 0) {
        return 0;
    }
    ++b.temple_level;
    b.turns_to_score = kTempleTurns;
    return kTemplePoints[static_cast<std::size_t>(b.temple_level - 1)];
}

bool is_temple(Building b) {
    return b == Building::Temple || b == Building::WaterTemple || b == Building::ForestTemple ||
           b == Building::MountainTemple;
}

bool is_leaf(Tech tech) {
    return std::ranges::none_of(kTechParent,
                                [&](const std::optional<Tech>& p) { return p == tech; });
}

// P's units in Tribes' list order: city units (city list order), then tribe-owned units. Units in
// no list (03b §4) are skipped.
template <typename S, typename F>
void for_each_unit_of(S& s, int p, const F& f) {
    auto& t = tribe(s, p);
    for (Slot c : t.cities) {
        for (Slot u : s.cities[c].units) {
            f(u);
        }
    }
    for (Slot u : t.tribe_units) {
        f(u);
    }
}

// The flags Tribes caches when it generates the active tribe's actions (04 §5): a pending level-up
// replaces every other action, including End turn.
void refresh_action_flags(GameState& s) {
    const int p = s.active_tribe;
    bool level_up = false;
    for (Slot c : tribe(s, p).cities) {
        level_up = level_up || s.cities[c].population >= s.cities[c].population_need;
    }
    s.leveling_up = level_up;
    const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(p));
    s.can_end_turn = level_up ? (s.can_end_turn & ~bit) : (s.can_end_turn | bit);
}

void remove_unit(GameState& s, Slot u) {
    Unit& unit = s.units[u];
    if (unit.city != kNoSlot) {
        s.cities[unit.city].units.erase_value(u);
    } else {
        tribe(s, unit.tribe).tribe_units.erase_value(u);
    }
    s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = kNoSlot;
    unit = Unit{};
}

// Replaces unit u by a new unit of type `type` on the same tile, keeping HP, max HP, kills,
// veteran flag and city (embark / disembark, 02 §3). The new unit takes the next actor id, goes to
// the end of its city's (or tribe's) unit list and starts Finished.
Slot replace_unit(GameState& s, Slot u, UnitType type, UnitType carried) {
    const Unit old = s.units[u];
    remove_unit(s, u);
    const Slot n = new_unit(s);
    Unit& unit = s.units[n];
    unit.type = type;
    unit.carried = carried;
    unit.tribe = old.tribe;
    unit.city = old.city;
    unit.x = old.x;
    unit.y = old.y;
    unit.hp = old.hp;
    unit.max_hp = old.max_hp;
    unit.kills = old.kills;
    unit.veteran = old.veteran;
    unit.status = TurnStatus::Finished;
    s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = n;
    if (unit.city != kNoSlot) {
        s.cities[unit.city].units.push_back(n);
    } else {
        tribe(s, unit.tribe).tribe_units.push_back(n);
    }
    return n;
}

// Places unit u on (x, y) and clears its tribe's view around it: radius 2 on a mountain or for a
// Battleship, else 1 (02 §3 step 1).
void place_unit(GameState& s, Slot u, int x, int y) {
    Unit& unit = s.units[u];
    s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = kNoSlot;
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    const Terrain t = s.terrain[static_cast<std::size_t>(s.index(x, y))];
    const int radius = (t == Terrain::Mountain || unit.type == UnitType::Battleship) ? 2 : 1;
    clear_view(s, unit.tribe, x, y, radius);
}

// A killed unit (failed push): removed, its owner loses the unit's points (02 §4).
void kill_unit(GameState& s, Slot u) {
    tribe(s, s.units[u].tribe).score -= kUnitPoints[static_cast<std::size_t>(s.units[u].type)];
    remove_unit(s, u);
}

// 02 §4 target rule for a push from the unit's tile to (x, y).
bool try_push(GameState& s, Slot u, int x, int y) {
    const auto i = static_cast<std::size_t>(s.index(x, y));
    Unit& unit = s.units[u];
    if (s.unit_at[i] != kNoSlot) {
        return false;
    }
    const Terrain t = s.terrain[i];
    if (t == Terrain::Mountain) {
        if (!tribe(s, unit.tribe).has_tech(Tech::Climbing)) {
            return false;
        }
        place_unit(s, u, x, y);
        return true;
    }
    if (t == Terrain::ShallowWater || t == Terrain::DeepWater) {
        if (is_naval(unit.type)) {
            return true;  // 02 §4 quirk: reports success without moving
        }
        const Slot owner = s.city_owner[i];
        if (s.building[i] == Building::Port && owner != kNoSlot &&
            s.cities[owner].tribe == unit.tribe) {
            s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = kNoSlot;
            unit.x = static_cast<std::uint8_t>(x);
            unit.y = static_cast<std::uint8_t>(y);
            replace_unit(s, u, UnitType::Boat, unit.type);  // embark (no view clearing)
            return true;
        }
        return false;
    }
    place_unit(s, u, x, y);
    return true;
}

// Push (02 §4): tries the fixed offset order; the unit (if it was not replaced) becomes Pushed.
// If no target works, the unit is killed.
void push(GameState& s, Slot u) {
    constexpr std::array<std::array<int, 2>, 8> kOffsets = {
        {{0, 1}, {-1, 0}, {0, -1}, {1, 0}, {-1, 1}, {-1, -1}, {1, -1}, {1, 1}}};
    const int x0 = s.units[u].x;
    const int y0 = s.units[u].y;
    const std::int32_t id = s.units[u].id;
    bool pushed = false;
    for (const auto& [dx, dy] : kOffsets) {
        if (s.on_board(x0 + dx, y0 + dy) && try_push(s, u, x0 + dx, y0 + dy)) {
            pushed = true;
            break;
        }
    }
    if (s.units[u].id == id) {  // still the same unit (not replaced by an embark)
        s.units[u].status = TurnStatus::Pushed;
        if (!pushed) {
            kill_unit(s, u);
        }
    }
}

// Creates a unit of `type` on (x, y), Finished, owned by city c (or by the tribe if c is kNoSlot).
// A full city silently ignores the add: the unit is then in no list (03b §4 quirk).
Slot create_unit(GameState& s, int p, Slot c, UnitType type, int x, int y, UnitType carried) {
    const Slot u = new_unit(s);
    Unit& unit = s.units[u];
    unit.type = type;
    unit.carried = carried;
    unit.tribe = static_cast<std::int16_t>(p);
    unit.city = c;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    const UnitType hp_type = is_naval(type) ? carried : type;
    unit.hp = static_cast<std::int16_t>(kUnitMaxHp[static_cast<std::size_t>(hp_type)]);
    unit.max_hp = unit.hp;
    unit.status = TurnStatus::Finished;
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
    if (c == kNoSlot) {
        tribe(s, p).tribe_units.push_back(u);
    } else if (static_cast<int>(s.cities[c].units.size()) < s.cities[c].level + 1) {
        s.cities[c].units.push_back(u);
    }
    return u;
}

bool traversable(const GameState& s, const TribeState& t, int x, int y) {
    switch (s.terrain[static_cast<std::size_t>(s.index(x, y))]) {
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

// Explorer walk (03b §6). Full obs: clearing view reveals nothing, so no network recomputation.
void explore(GameState& s, int p, int x, int y, Chance& chance) {
    constexpr int kSteps = 15;
    constexpr int kTries = 45;
    const TribeState& t = tribe(s, p);
    for (int step = 0; step < kSteps; ++step) {
        for (int k = 0; k < kTries; ++k) {
            std::array<std::array<int, 2>, 8> nb{};
            int count = 0;
            for (int nx = x - 1; nx <= x + 1; ++nx) {
                for (int ny = y - 1; ny <= y + 1; ++ny) {
                    if ((nx != x || ny != y) && s.on_board(nx, ny)) {
                        nb[static_cast<std::size_t>(count++)] = {nx, ny};
                    }
                }
            }
            const auto [tx, ty] = nb[static_cast<std::size_t>(chance.explorer_neighbour(count))];
            if (traversable(s, t, tx, ty)) {
                x = tx;
                y = ty;
                clear_view(s, p, x, y, 1);
                break;
            }
        }
    }
}

// "All researched" re-check (01 quirk: only after a leaf tech).
void mark_researched(TribeState& t, Tech tech) {
    t.techs |= 1U << static_cast<unsigned>(tech);
    if (is_leaf(tech)) {
        t.all_researched = t.techs == (1U << kNumTechs) - 1;
    }
}

[[nodiscard]] bool researchable(const TribeState& t, Tech tech) {
    const auto parent = kTechParent[static_cast<std::size_t>(tech)];
    return !t.has_tech(tech) && (!parent || t.has_tech(*parent));
}

// ---- diplomacy (04 §6.2) ----

void update_allegiance(GameState& s, int v, int i, int j) {
    auto& aij = s.allegiance[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    if (aij + v > kAllegianceMax || aij + v < -kAllegianceMax) {
        v = (v > 0 ? 1 : -1) * (kAllegianceMax - std::abs(aij));
    }
    aij = static_cast<std::int16_t>(aij + v);
    auto& aji = s.allegiance[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
    aji = static_cast<std::int16_t>(aji + v);
}

void consequences(GameState& s, int v, int i, int t) {
    const int w = v / -2;  // truncates toward zero, as in Java
    for (int k = 0; k < s.num_tribes; ++k) {
        if (k != i && s.allegiance[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)] < 0) {
            update_allegiance(s, w, k, i);
        }
    }
}

// ---- tribe actions ----

int research_cost(const TribeState& t, Tech tech) {
    const int cost =
        4 + (kTechTier[static_cast<std::size_t>(tech)] * static_cast<int>(t.cities.size()));
    // 01 quirk: Philosophy gives 80 % off; Java truncates the double product.
    return t.has_tech(Tech::Philosophy) ? static_cast<int>(cost * 0.2) : cost;
}

bool research_legal(const TribeState& t, Tech tech) {
    return researchable(t, tech) && t.stars >= research_cost(t, tech);
}

void research(GameState& s, int p, Tech tech) {
    TribeState& t = tribe(s, p);
    const auto ti = static_cast<std::size_t>(tech);
    const int cost = research_cost(t, tech);
    check(research_legal(t, tech), "research: illegal");
    t.stars -= cost;
    mark_researched(t, tech);
    t.score += kTechTier[ti] * 100;
    if (t.all_researched) {
        unlock(t, Building::TowerOfWisdom);
    }
}

// 04 §6.3: no check that q is alive or met.
bool declare_war_legal(const GameState& s, int p, int q) {
    return q != p && q >= 0 && q < s.num_tribes &&
           s.allegiance[static_cast<std::size_t>(p)][static_cast<std::size_t>(q)] >
               kWarAllegiance &&
           !tribe(s, p).declared_war_this_turn;
}

void declare_war(GameState& s, int p, int q) {
    TribeState& t = tribe(s, p);
    const int a = s.allegiance[static_cast<std::size_t>(p)][static_cast<std::size_t>(q)];
    check(declare_war_legal(s, p, q), "declare war: illegal");
    t.declared_war_this_turn = true;
    ++t.wars_declared;
    update_allegiance(s, kWarAllegiance - a, p, q);
    consequences(s, kWarAllegiance, p, q);
}

// 04 §6.4: the 30-per-turn cap is checked before sending, not against the total.
bool send_stars_legal(const GameState& s, int p, int q, int n) {
    const TribeState& t = tribe(s, p);
    return q != p && q >= 0 && q < s.num_tribes && n >= 1 && n < kSendStarsBound && n < t.stars &&
           t.stars_sent_this_turn < kSendStarsTurnCap;
}

void send_stars(GameState& s, int p, int q, int n) {
    TribeState& t = tribe(s, p);
    check(send_stars_legal(s, p, q, n), "send stars: illegal");
    t.stars -= n;
    add_stars(tribe(s, q), n);
    t.stars_sent_this_turn += n;
    t.stars_sent_total += n;
    t.score += n;
    update_allegiance(s, n, p, q);
    consequences(s, n, p, q);
}

// 03b §8.
bool road_legal(const GameState& s, int p, int x, int y) {
    if (!s.on_board(x, y)) {
        return false;
    }
    const TribeState& t = tribe(s, p);
    const auto i = static_cast<std::size_t>(s.index(x, y));
    const Terrain ter = s.terrain[i];
    const Slot owner = s.city_owner[i];
    const Slot u = s.unit_at[i];
    return t.has_tech(Tech::Roads) && t.stars >= kRoadCost && t.visible.test(i) &&
           (ter == Terrain::Plain || ter == Terrain::Forest || ter == Terrain::Village) &&
           (owner == kNoSlot || s.cities[owner].tribe == p) && !s.network.test(i) &&
           (u == kNoSlot || s.units[u].tribe == p);
}

void build_road(GameState& s, int p, int x, int y) {
    TribeState& t = tribe(s, p);
    check(road_legal(s, p, x, y), "build road: illegal");
    t.stars -= kRoadCost;
    set_network(s, x, y, true);
}

// ---- city actions ----

int gather_cost(Resource r) { return r == Resource::Whales ? 0 : kGatherCost; }

// 03a §4: gathering the resource on (x, y), a tile of city c.
bool gather_legal(const GameState& s, Slot c, int x, int y) {
    if (!is_city_tile(s, c, x, y)) {
        return false;
    }
    const TribeState& t = tribe(s, s.cities[c].tribe);
    const Resource r = s.resource[static_cast<std::size_t>(s.index(x, y))];
    constexpr std::array<Tech, 4> kTech = {Tech::Fishing, Tech::Organization, Tech::Hunting,
                                           Tech::Whaling};
    return r <= Resource::Whales && t.has_tech(kTech[static_cast<std::size_t>(r)]) &&
           t.stars >= gather_cost(r);
}

void gather(GameState& s, Slot c, int x, int y, Resource r) {
    City& city = s.cities[c];
    TribeState& t = tribe(s, city.tribe);
    const auto i = static_cast<std::size_t>(s.index(x, y));
    const int cost = gather_cost(r);
    check(s.resource[i] == r && gather_legal(s, c, x, y), "gather: illegal");
    s.resource[i] = Resource::None;
    t.stars -= cost;
    if (r == Resource::Whales) {
        add_stars(t, kWhaleStars);
    } else {
        add_population(s, city, city.tribe, 1);
    }
}

bool can_level_up(const City& c) { return c.population >= c.population_need; }

// 03a §2: the reward pair offered for the city's current level (0: level 1, ..., 3: level >= 4).
int level_up_pair(const City& c) { return std::min<int>(c.level, 4) - 1; }

void level_up(GameState& s, Slot c, LevelUpBonus b, Chance& chance) {
    City& city = s.cities[c];
    TribeState& t = tribe(s, city.tribe);
    const int pair = level_up_pair(city);
    check(can_level_up(city) && static_cast<int>(b) / 2 == pair, "level up: illegal");
    if (pair == 3) {
        unlock(t, Building::ParkOfFortune);
    }
    const int points = 50 - (5 * (pair + 2));  // 01: 50 - 5 x reward level
    t.score += points;
    city.points_worth += points;
    city.population = static_cast<std::int16_t>(city.population - city.population_need);
    ++city.level;
    city.population_need = static_cast<std::int16_t>(city.level + 1);
    switch (b) {
        case LevelUpBonus::Workshop:
            ++city.production_bonus;
            break;
        case LevelUpBonus::CityWall:
            city.walls = true;
            break;
        case LevelUpBonus::Resources:
            add_stars(t, kLevelUpResourceStars);
            break;
        case LevelUpBonus::PopGrowth:
            add_population(s, city, city.tribe, kLevelUpPopGrowth);
            break;
        case LevelUpBonus::BorderGrowth:
            ++city.border_radius;
            claim_tiles(s, c, city.border_radius);
            break;
        case LevelUpBonus::Park:
            t.score += kParkPoints;
            city.points_worth += kParkPoints;
            break;
        case LevelUpBonus::Explorer:
            explore(s, city.tribe, city.x, city.y, chance);
            break;
        case LevelUpBonus::SuperUnit: {
            const Slot occupant = s.unit_at[static_cast<std::size_t>(s.index(city.x, city.y))];
            if (occupant != kNoSlot) {
                push(s, occupant);
            }
            create_unit(s, city.tribe, c, UnitType::SuperUnit, city.x, city.y, UnitType::Warrior);
            break;
        }
    }
}

// 03a §4: naval units and the Super unit are not spawnable.
bool spawn_legal(const GameState& s, Slot c, UnitType type) {
    const City& city = s.cities[c];
    const TribeState& t = tribe(s, city.tribe);
    const auto ui = static_cast<std::size_t>(type);
    const auto tile = static_cast<std::size_t>(s.index(city.x, city.y));
    const auto tech = kUnitTech[ui];
    return type <= UnitType::MindBender && t.stars >= kUnitCost[ui] &&
           static_cast<int>(city.units.size()) < city.level + 1 && s.unit_at[tile] == kNoSlot &&
           (!tech || t.has_tech(*tech));
}

void spawn(GameState& s, Slot c, UnitType type) {
    City& city = s.cities[c];
    TribeState& t = tribe(s, city.tribe);
    const auto ui = static_cast<std::size_t>(type);
    check(spawn_legal(s, c, type), "spawn: illegal");
    create_unit(s, city.tribe, c, type, city.x, city.y, UnitType::Warrior);
    t.stars -= kUnitCost[ui];
    t.score += kUnitPoints[ui];
}

// 03a §4: Clear, Burn or Grow forest on (x, y), a tile of city c.
bool forest_legal(const GameState& s, Slot c, int x, int y, ActionType type) {
    if (!is_city_tile(s, c, x, y)) {
        return false;
    }
    const TribeState& t = tribe(s, s.cities[c].tribe);
    const Terrain ter = s.terrain[static_cast<std::size_t>(s.index(x, y))];
    switch (type) {
        case ActionType::ClearForest:
            return ter == Terrain::Forest && t.has_tech(Tech::Forestry);
        case ActionType::BurnForest:
            return ter == Terrain::Forest && t.has_tech(Tech::Chivalry) &&
                   t.stars >= kForestBurnCost;
        default:  // GrowForest
            return ter == Terrain::Plain && t.has_tech(Tech::Spiritualism) &&
                   t.stars >= kForestGrowCost;
    }
}

void forest_action(GameState& s, Slot c, int x, int y, ActionType type) {
    TribeState& t = tribe(s, s.cities[c].tribe);
    const auto i = static_cast<std::size_t>(s.index(x, y));
    check(forest_legal(s, c, x, y, type), "forest action: illegal");
    switch (type) {
        case ActionType::ClearForest:
            s.terrain[i] = Terrain::Plain;
            add_stars(t, kForestClearStars);
            break;
        case ActionType::BurnForest:
            s.terrain[i] = Terrain::Plain;
            s.resource[i] = Resource::Crops;
            t.stars -= kForestBurnCost;
            break;
        default:  // GrowForest
            s.terrain[i] = Terrain::Forest;
            s.resource[i] = Resource::None;
            t.stars -= kForestGrowCost;
            break;
    }
}

// ---- buildings (03a §3) ----

bool is_monument(Building b) {
    return b != Building::None && static_cast<int>(b) >= kFirstMonument;
}
bool is_base(Building b) {
    return b == Building::Farm || b == Building::Mine || b == Building::LumberHut;
}
Building matching(Building b) {
    switch (b) {
        case Building::Port:
            return Building::CustomsHouse;
        case Building::Farm:
            return Building::Windmill;
        case Building::Mine:
            return Building::Forge;
        case Building::LumberHut:
            return Building::Sawmill;
        case Building::CustomsHouse:
            return Building::Port;
        case Building::Windmill:
            return Building::Farm;
        case Building::Forge:
            return Building::Mine;
        case Building::Sawmill:
            return Building::LumberHut;
        default:
            return Building::None;
    }
}

int bonus(Building b) { return kBuildingBonus[static_cast<std::size_t>(b)]; }

// Accumulated points of a temple: the points of every level it has reached.
int temple_points(const BuildingEntry& b) {
    int sum = 0;
    for (int l = 0; std::cmp_less(l, b.temple_level); ++l) {
        sum += kTemplePoints[static_cast<std::size_t>(l)];
    }
    return sum;
}

const BuildingEntry* record_at(const City& c, int x, int y) {
    for (const BuildingEntry& b : c.buildings) {
        if (std::cmp_equal(b.x, x) && std::cmp_equal(b.y, y)) {
            return &b;
        }
    }
    return nullptr;
}

// Neighbour-pair bonuses with Tribes' loop quirks (03a §3): the receiving city is not reset between
// neighbours, and a neighbour in a city of another tribe (or outside all borders) ends the loop.
// One neighbour (nx, ny) of pair_bonus. `to` is the city receiving the bonus; as in Tribes it
// keeps the last foreign owner across neighbours. Returns false where Tribes' loop returns early
// (a matching building in a city that is not the tribe's).
bool pair_neighbour(GameState& s, Slot c, const BuildingEntry& b, int nx, int ny, bool population,
                    int sign, Slot& to) {
    const auto n = static_cast<std::size_t>(s.index(nx, ny));
    if (s.building[n] != matching(b.type)) {
        return true;
    }
    const int p = s.cities[c].tribe;
    const Slot owner = s.city_owner[n];
    if (owner != c) {
        if (owner == kNoSlot || !tribe(s, p).cities.contains(owner)) {
            return false;
        }
        to = owner;
    }
    const BuildingEntry* other = record_at(s.cities[owner], nx, ny);
    if (other == nullptr) {
        return true;
    }
    const int v = sign * (is_base(b.type) ? bonus(other->type) : bonus(b.type));
    City& dest = s.cities[to];
    if (population) {
        add_population(s, dest, p, v);
    } else {
        dest.production_bonus = static_cast<std::int16_t>(std::max(0, dest.production_bonus + v));
    }
    return true;
}

void pair_bonus(GameState& s, Slot c, const BuildingEntry& b, bool population, bool matching_only,
                int sign) {
    if (is_base(b.type) && population && !matching_only) {
        add_population(s, s.cities[c], s.cities[c].tribe, sign * bonus(b.type));
    }
    Slot to = c;
    for (int nx = b.x - 1; nx <= b.x + 1; ++nx) {
        for (int ny = b.y - 1; ny <= b.y + 1; ++ny) {
            if ((std::cmp_equal(nx, b.x) && std::cmp_equal(ny, b.y)) || !s.on_board(nx, ny)) {
                continue;
            }
            if (!pair_neighbour(s, c, b, nx, ny, population, sign, to)) {
                return;
            }
        }
    }
}

// Building effects (03a §3), sign +1 on build, -1 on destroy; matching_only for capture (03b §3).
void building_effects(GameState& s, Slot c, const BuildingEntry& b, int sign, bool matching_only) {
    const int p = s.cities[c].tribe;
    TribeState& t = tribe(s, p);
    switch (b.type) {
        case Building::Farm:
        case Building::LumberHut:
        case Building::Mine:
        case Building::Windmill:
        case Building::Sawmill:
        case Building::Forge:
            pair_bonus(s, c, b, true, matching_only, sign);
            break;
        case Building::Port:
            if (!matching_only) {
                add_population(s, s.cities[c], p, sign * bonus(b.type));
            }
            pair_bonus(s, c, b, false, matching_only, sign);
            break;
        case Building::CustomsHouse:
            pair_bonus(s, c, b, false, matching_only, sign);
            break;
        case Building::Temple:
        case Building::WaterTemple:
        case Building::MountainTemple:
        case Building::ForestTemple:
            if (!matching_only) {
                add_population(s, s.cities[c], p, sign * bonus(b.type));
            }
            t.score += sign < 0 ? temple_points(b) : kTemplePoints[0];  // 03a quirk on destroy
            break;
        case Building::None:
            break;
        default:  // monuments
            if (!matching_only) {
                add_population(s, s.cities[c], p, sign * bonus(b.type));
            }
            t.score += sign * kMonumentPoints;
            break;
    }
}

bool terrain_allows(Building b, Terrain t) {
    switch (b) {
        case Building::Port:
            return t == Terrain::ShallowWater;
        case Building::Mine:
        case Building::MountainTemple:
            return t == Terrain::Mountain;
        case Building::LumberHut:
        case Building::ForestTemple:
            return t == Terrain::Forest;
        case Building::WaterTemple:
            return t == Terrain::ShallowWater || t == Terrain::DeepWater;
        default:
            return is_monument(b) ? (t == Terrain::Plain || t == Terrain::ShallowWater)
                                  : t == Terrain::Plain;
    }
}

// Windmill / Forge / Sawmill / Customs house: a matching building next to (x, y), and none of the
// same type in the city yet (03a §3).
bool pair_placement_ok(const GameState& s, Slot c, int x, int y, Building b) {
    const City& city = s.cities[c];
    bool adjacent = false;
    bool duplicate = false;
    const int r = city.level < 4 ? 1 : 2;
    for (int nx = x - 1; nx <= x + 1; ++nx) {
        for (int ny = y - 1; ny <= y + 1; ++ny) {
            adjacent =
                adjacent || ((nx != x || ny != y) && s.on_board(nx, ny) &&
                             s.building[static_cast<std::size_t>(s.index(nx, ny))] == matching(b));
        }
    }
    for (int nx = city.x - r; nx <= city.x + r; ++nx) {
        for (int ny = city.y - r; ny <= city.y + r; ++ny) {
            duplicate = duplicate || (is_city_tile(s, c, nx, ny) &&
                                      s.building[static_cast<std::size_t>(s.index(nx, ny))] == b);
        }
    }
    return adjacent && !duplicate;
}

bool build_legal(const GameState& s, Slot c, int x, int y, Building b) {
    const City& city = s.cities[c];
    const TribeState& t = s.tribes[static_cast<std::size_t>(city.tribe)];
    const auto bi = static_cast<std::size_t>(b);
    const auto i = static_cast<std::size_t>(s.index(x, y));
    const auto tech = kBuildingTech[bi];
    if (!is_city_tile(s, c, x, y) || s.building[i] != Building::None ||
        (kBuildingCost[bi] > 0 && t.stars < kBuildingCost[bi]) || (tech && !t.has_tech(*tech)) ||
        !terrain_allows(b, s.terrain[i])) {
        return false;
    }
    if ((b == Building::Farm && s.resource[i] != Resource::Crops) ||
        (b == Building::Mine && s.resource[i] != Resource::Ore)) {
        return false;
    }
    const bool needs_pair = b == Building::Windmill || b == Building::Forge ||
                            b == Building::Sawmill || b == Building::CustomsHouse;
    if (needs_pair && !pair_placement_ok(s, c, x, y, b)) {
        return false;
    }
    return !is_monument(b) || t.monuments[bi - kFirstMonument] == MonumentStatus::Available;
}

void build(GameState& s, Slot c, int x, int y, Building b) {
    check(build_legal(s, c, x, y, b), "build: illegal");
    City& city = s.cities[c];
    TribeState& t = tribe(s, city.tribe);
    const auto i = static_cast<std::size_t>(s.index(x, y));
    t.stars -= kBuildingCost[static_cast<std::size_t>(b)];
    s.building[i] = b;
    s.resource[i] = Resource::None;
    BuildingEntry e{
        .x = static_cast<std::uint8_t>(x), .y = static_cast<std::uint8_t>(y), .type = b};
    if (is_temple(b)) {
        e.temple_level = 1;
        e.turns_to_score = kTempleTurns;
    }
    building_effects(s, c, e, +1, false);
    city.buildings.push_back(e);
    if (b == Building::Port) {
        set_network(s, x, y, true);
    }
    if (is_monument(b)) {
        monument(t, b) = MonumentStatus::Built;
    }
    if (b == Building::LumberHut) {
        s.terrain[i] = Terrain::Plain;  // 03a quirk
    }
}

// 03a §3.
bool destroy_legal(const GameState& s, Slot c, int x, int y) {
    const City& city = s.cities[c];
    return is_city_tile(s, c, x, y) &&
           s.building[static_cast<std::size_t>(s.index(x, y))] != Building::None &&
           record_at(city, x, y) != nullptr && tribe(s, city.tribe).has_tech(Tech::Construction);
}

void destroy(GameState& s, Slot c, int x, int y) {
    City& city = s.cities[c];
    const auto i = static_cast<std::size_t>(s.index(x, y));
    const BuildingEntry* rec = record_at(city, x, y);
    check(destroy_legal(s, c, x, y), "destroy: illegal");
    const BuildingEntry e = *rec;
    s.building[i] = Building::None;
    if (e.type == Building::Port) {
        set_network(s, x, y, false);
    }
    building_effects(s, c, e, -1, false);
    for (std::size_t k = 0; k < city.buildings.size(); ++k) {
        if (std::cmp_equal(city.buildings[k].x, x) && std::cmp_equal(city.buildings[k].y, y)) {
            city.buildings.erase_at(k);
            break;
        }
    }
}

// ---- unit actions ----

// 03b §7: Boat -> Ship, Ship -> Battleship; the replacement keeps the status (unlike embarking).
UnitType upgraded(UnitType t) {
    return t == UnitType::Boat ? UnitType::Ship : UnitType::Battleship;
}

bool upgrade_legal(const GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    const TribeState& t = tribe(s, unit.tribe);
    return ((unit.type == UnitType::Boat && t.has_tech(Tech::Sailing)) ||
            (unit.type == UnitType::Ship && t.has_tech(Tech::Navigation))) &&
           t.stars >= kUnitCost[static_cast<std::size_t>(upgraded(unit.type))];
}

void upgrade(GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    TribeState& t = tribe(s, unit.tribe);
    const UnitType next = upgraded(unit.type);
    const int cost = kUnitCost[static_cast<std::size_t>(next)];
    check(upgrade_legal(s, u), "upgrade: illegal");
    const TurnStatus status = unit.status;
    t.stars -= cost;
    const Slot n = replace_unit(s, u, next, unit.carried);
    s.units[n].status = status;
}

// 03b §7.
bool make_veteran_legal(const Unit& unit) {
    return unit.type != UnitType::SuperUnit && unit.kills >= kVeteranKills && !unit.veteran;
}

void make_veteran(GameState& s, Slot u) {
    Unit& unit = s.units[u];
    check(make_veteran_legal(unit), "make veteran: illegal");
    unit.veteran = true;
    unit.max_hp = static_cast<decltype(unit.max_hp)>(unit.max_hp + kVeteranPlusHp);
    unit.hp = unit.max_hp;
}

bool can_recover(const GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    return unit.status == TurnStatus::Fresh && unit.hp > 0 && unit.hp < unit.max_hp;
}

// 03b §7.
void recover(GameState& s, Slot u) {
    check(can_recover(s, u), "recover: illegal");
    Unit& unit = s.units[u];
    const Slot owner = s.city_owner[static_cast<std::size_t>(s.index(unit.x, unit.y))];
    const bool own_borders = owner != kNoSlot && s.cities[owner].tribe == unit.tribe;
    const int heal = kRecoverHp + (own_borders ? kRecoverHp : 0);
    unit.hp = static_cast<std::int16_t>(std::min<int>(unit.hp + heal, unit.max_hp));
    unit.status = TurnStatus::Finished;
}

void move(GameState& s, Slot u, int x, int y) {
    Unit& unit = s.units[u];
    check(can_move(unit) && s.on_board(x, y), "move: unit cannot move");
    MoveTargets targets;
    move_targets(s, u, targets);
    const auto dest = static_cast<std::uint16_t>(s.index(x, y));
    const std::span<const std::uint16_t> reachable(targets.tiles.data(),
                                                   static_cast<std::size_t>(targets.count));
    check(std::ranges::find(reachable, dest) != reachable.end(), "move: destination not reachable");
    place_unit(s, u, x, y);
    const Terrain t = s.terrain[dest];
    const bool water = t == Terrain::ShallowWater || t == Terrain::DeepWater;
    if (is_naval(unit.type) && !water) {
        replace_unit(s, u, unit.carried, UnitType::Warrior);  // disembark
    } else if (!is_naval(unit.type) && s.building[dest] == Building::Port) {
        replace_unit(s, u, UnitType::Boat, unit.type);  // embark
    } else {
        unit.status = status_after_move(unit);
    }
}

// 03b §5.
bool examine_legal(const GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    return unit.status == TurnStatus::Fresh &&
           s.resource[static_cast<std::size_t>(s.index(unit.x, unit.y))] == Resource::Ruins &&
           !tribe(s, unit.tribe).cities.empty();
}

void examine(GameState& s, Slot u, Chance& chance) {
    Unit& unit = s.units[u];
    const int p = unit.tribe;
    TribeState& t = tribe(s, p);
    const int x = unit.x;
    const int y = unit.y;
    const auto i = static_cast<std::size_t>(s.index(x, y));
    check(examine_legal(s, u), "examine: illegal");
    const std::int32_t id = unit.id;
    const Slot handler = t.cities.contains(t.capital) ? t.capital : t.cities[0];
    switch (chance.ruin_bonus(t.all_researched)) {
        case RuinBonus::SuperUnit: {
            const bool water =
                s.terrain[i] == Terrain::ShallowWater || s.terrain[i] == Terrain::DeepWater;
            push(s, u);
            const Slot n =
                create_unit(s, p, kNoSlot, water ? UnitType::Battleship : UnitType::SuperUnit, x, y,
                            UnitType::Warrior);
            if (water) {  // 03b §5 quirk: Battleship's constructor HP -1 is never overwritten
                s.units[n].hp = -1;
                s.units[n].max_hp = -1;
            }
            break;
        }
        case RuinBonus::Research: {
            if (t.all_researched) {
                break;
            }
            std::array<Tech, kNumTechs> candidates{};
            int n = 0;
            for (int k = 0; k < kNumTechs; ++k) {
                if (researchable(t, static_cast<Tech>(k))) {
                    candidates[static_cast<std::size_t>(n++)] = static_cast<Tech>(k);
                }
            }
            if (n > 0) {
                mark_researched(t, candidates[static_cast<std::size_t>(chance.random_tech(n))]);
            }
            break;
        }
        case RuinBonus::PopGrowth:
            add_population(s, s.cities[handler], p, kLevelUpPopGrowth);
            break;
        case RuinBonus::Explorer:
            explore(s, p, x, y, chance);
            break;
        case RuinBonus::Resources:
            add_stars(t, kRuinStars);
            break;
    }
    s.resource[i] = Resource::None;
    if (s.units[u].id == id) {
        s.units[u].status = TurnStatus::Finished;
    }
}

// "Move one" (03b §3 village step 5): one unit of tribe p joins city dest.
bool has_room(const City& c) { return static_cast<int>(c.units.size()) < c.level + 1; }

void move_last_unit(GameState& s, Slot from, Slot to) {
    City& src = s.cities[from];
    const Slot u = src.units.back();
    src.units.erase_at(src.units.size() - 1);
    // Tribes' City.addUnit would silently drop a unit into a full city; no capture path reaches
    // that (move-all checks room first, move-one targets a city just emptied), so fail loud.
    check(has_room(s.cities[to]), "move unit: destination city full");
    s.cities[to].units.push_back(u);
    s.units[u].city = to;
}

void move_one_to(GameState& s, int p, Slot dest, Chance& chance) {
    TribeState& t = tribe(s, p);
    if (t.cities.contains(t.capital) && !s.cities[t.capital].units.empty()) {
        move_last_unit(s, t.capital, dest);
        return;
    }
    InlineVector<Slot, kMaxCities> others;
    for (Slot c : t.cities) {
        if (c != t.capital) {
            others.push_back(c);
        }
    }
    chance.shuffle_cities(std::span<std::uint16_t>(others.begin(), others.size()));
    for (Slot c : others) {
        if (!s.cities[c].units.empty()) {
            move_last_unit(s, c, dest);
            return;
        }
    }
}

// Tribe q's units of city c move out (03b §3 step 6, Board.moveAllFromCity).
void move_all_from(GameState& s, int q, Slot c, Chance& chance) {
    TribeState& t = tribe(s, q);
    City& from = s.cities[c];
    if (t.cities.contains(t.capital)) {
        while (has_room(s.cities[t.capital]) && !from.units.empty()) {
            move_last_unit(s, c, t.capital);
        }
    }
    if (from.units.empty()) {
        return;
    }
    InlineVector<Slot, kMaxCities> others;
    for (Slot o : t.cities) {
        if (o != t.capital) {
            others.push_back(o);
        }
    }
    chance.shuffle_cities(std::span<std::uint16_t>(others.begin(), others.size()));
    for (Slot o : others) {
        while (has_room(s.cities[o]) && !from.units.empty()) {
            move_last_unit(s, c, o);
        }
        if (from.units.empty()) {
            return;
        }
    }
    for (Slot u : from.units) {  // leftovers become tribe-owned, front to back
        t.tribe_units.push_back(u);
        s.units[u].city = kNoSlot;
    }
    from.units.clear();
}

// Elimination (03b §3 step 8, Tribe.manageLoss): Q has no cities left, so only its tribe-owned
// units are on any list Tribes walks; units in no list stay (03b §4).
void eliminate(GameState& s, int q) {
    TribeState& t = tribe(s, q);
    t.result = Result::Loss;
    for (Slot u : t.tribe_units) {
        s.unit_at[static_cast<std::size_t>(s.index(s.units[u].x, s.units[u].y))] = kNoSlot;
        s.units[u] = Unit{};
    }
    t.tribe_units.clear();
}

// 03b §3, city capture.
void capture_city(GameState& s, Slot u, Slot c, Chance& chance) {
    Unit& unit = s.units[u];
    const int p = unit.tribe;
    City& city = s.cities[c];
    const int q = city.tribe;
    const int x = city.x;
    const int y = city.y;
    check(q != p && std::cmp_equal(x, unit.x) && std::cmp_equal(y, unit.y),
          "capture city: illegal");
    tribe(s, q).score -= city.points_worth;
    tribe(s, p).score += city.points_worth;
    update_allegiance(s, kCaptureRepercussion, p, q);
    consequences(s, kCaptureRepercussion, p, q);
    unit.status = TurnStatus::Finished;
    tribe(s, p).cities.push_back(c);
    city.tribe = static_cast<std::int16_t>(p);
    for (const BuildingEntry& b : city.buildings) {
        building_effects(s, c, b, +1, true);
    }
    tribe(s, q).cities.erase_value(c);
    for (const BuildingEntry& b : city.buildings) {
        if (is_base(b.type) || b.type == Building::Port) {
            building_effects(s, c, b, -1, true);
        }
    }
    move_all_from(s, q, c, chance);
    move_one_to(s, p, c, chance);
    if (tribe(s, q).cities.empty()) {
        eliminate(s, q);
    }
    set_network(s, x, y, true);
}

void capture_village(GameState& s, Slot u, Chance& chance) {
    Unit& unit = s.units[u];
    const int p = unit.tribe;
    const int x = unit.x;
    const int y = unit.y;
    const auto i = static_cast<std::size_t>(s.index(x, y));
    unit.status = TurnStatus::Finished;
    const Slot c = new_city(s);
    City& city = s.cities[c];
    city.tribe = static_cast<std::int16_t>(p);
    city.x = static_cast<std::uint8_t>(x);
    city.y = static_cast<std::uint8_t>(y);
    tribe(s, p).cities.push_back(c);
    clear_view(s, p, x, y, 1);
    set_network(s, x, y, true);
    claim_tiles(s, c, city.border_radius);
    s.terrain[i] = Terrain::City;
    move_one_to(s, p, c, chance);
    tribe(s, p).score += kCityCentrePoints;
    set_network(s, x, y, true);
}

// 03b §3: the capture unit u may make, as the logged target (0 = the village on its tile, else the
// city's actor id), or nullopt. [Tribes quirk] The city is the tile's border owner, which must be
// centred on the tile: a city founded inside another city's borders cannot be captured.
std::optional<std::int32_t> capture_target(const GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    if (unit.status != TurnStatus::Fresh) {
        return std::nullopt;
    }
    const auto i = static_cast<std::size_t>(s.index(unit.x, unit.y));
    if (s.terrain[i] == Terrain::Village) {
        return 0;
    }
    const Slot c = s.city_owner[i];
    if (s.terrain[i] != Terrain::City || c == kNoSlot) {
        return std::nullopt;
    }
    const City& city = s.cities[c];
    if (city.x != unit.x || city.y != unit.y || city.tribe == unit.tribe) {
        return std::nullopt;
    }
    return city.id;
}

void capture(GameState& s, Slot u, std::int32_t city_id, Chance& chance) {
    check(capture_target(s, u) == city_id, "capture: illegal");
    if (city_id == 0) {
        capture_village(s, u, chance);
    } else {
        capture_city(s, u, city_by_id(s, city_id), chance);
    }
}

void add_kill(TribeState& t) {
    if (++t.kills >= kGateOfPowerKills) {
        unlock(t, Building::GateOfPower);
    }
}

void add_unit_kill(Unit& u) {
    ++u.kills;
    if (u.type == UnitType::Knight) {
        u.status = TurnStatus::Attacked;  // 02 §1 persist
    }
}

// Java Math.round for the non-negative values combat produces: round half up, exactly.
long java_round(double v) {
    const double f = std::floor(v);
    return static_cast<long>(v - f >= 0.5 ? f + 1 : f);
}

int chebyshev_units(const Unit& a, const Unit& b) { return chebyshev(a.x, a.y, b.x, b.y); }

// 02 §2.
bool attack_legal(const GameState& s, Slot au, Slot tu) {
    const Unit& a = s.units[au];
    const Unit& t = s.units[tu];
    return t.tribe != a.tribe && can_attack(a) && a.type != UnitType::MindBender &&
           tribe(s, a.tribe).visible.test(static_cast<std::size_t>(s.index(t.x, t.y))) &&
           chebyshev_units(a, t) <= kUnitRange[static_cast<std::size_t>(a.type)];
}

// 02 §2, in Tribes' operation order (hp / maxHp first, then x stat).
void attack(GameState& s, Slot au, std::int32_t target_id) {
    const Slot tu = unit_by_id(s, target_id);
    Unit& a = s.units[au];
    Unit& t = s.units[tu];
    const auto ai = static_cast<std::size_t>(a.type);
    const auto ti = static_cast<std::size_t>(t.type);
    TribeState& at = tribe(s, a.tribe);
    TribeState& tt = tribe(s, t.tribe);
    const auto tile = static_cast<std::size_t>(s.index(t.x, t.y));
    check(attack_legal(s, au, tu), "attack: illegal");
    a.status = status_after_attack(a);
    at.pacifist_count = 0;

    double attack_force = kUnitAttack[ai] * (static_cast<double>(a.hp) / a.max_hp);
    double defence_force = kUnitDefence[ti] * (static_cast<double>(t.hp) / t.max_hp);
    const Terrain ter = s.terrain[tile];
    if (ter == Terrain::City) {
        const Slot c = s.city_owner[tile];
        if (c != kNoSlot && tt.cities.contains(c)) {
            if (s.cities[c].walls) {
                defence_force *= kDefenceInWalls;
            } else if (t.type == UnitType::Warrior || t.type == UnitType::Rider ||
                       t.type == UnitType::Archer || t.type == UnitType::Defender ||
                       t.type == UnitType::Swordsman || t.type == UnitType::Knight) {
                defence_force *= kDefenceBonus;
            }
        }
    } else if ((ter == Terrain::Mountain && tt.has_tech(Tech::Meditation)) ||
               ((ter == Terrain::ShallowWater || ter == Terrain::DeepWater) &&
                tt.has_tech(Tech::Aquatism)) ||
               (ter == Terrain::Forest && tt.has_tech(Tech::Archery))) {
        defence_force *= kDefenceBonus;
    }
    const double total = attack_force + defence_force;
    const long attack_result =
        java_round((attack_force / total) * kUnitAttack[ai] * kAttackModifier);
    const long defence_result =
        java_round((defence_force / total) * kUnitDefence[ti] * kAttackModifier);

    const int ap = a.tribe;
    const int tp = t.tribe;
    update_allegiance(s, kAttackRepercussion, ap, tp);
    consequences(s, kAttackRepercussion, ap, tp);
    if (t.hp <= attack_result) {
        update_allegiance(s, kAttackRepercussion, ap, tp);  // 02 quirk: applied twice on a kill
        consequences(s, kAttackRepercussion, ap, tp);
        add_unit_kill(a);
        add_kill(at);
        const int tx = t.x;
        const int ty = t.y;
        kill_unit(s, tu);
        switch (a.type) {
            case UnitType::Defender:
            case UnitType::Swordsman:
            case UnitType::Rider:
            case UnitType::Warrior:
            case UnitType::Knight:
            case UnitType::SuperUnit:
                try_push(s, au, tx, ty);  // advance; status unchanged
                break;
            default:
                break;
        }
    } else {
        t.hp = static_cast<std::int16_t>(t.hp - attack_result);
        if (chebyshev_units(a, t) <= kUnitRange[ti]) {
            a.hp = static_cast<std::int16_t>(a.hp - defence_result);
            if (a.hp <= 0) {
                add_unit_kill(t);
                add_kill(tt);
                kill_unit(s, au);
            }
        }
    }
}

bool disband_legal(const GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    return unit.status == TurnStatus::Fresh && tribe(s, unit.tribe).has_tech(Tech::FreeSpirit);
}

void disband(GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    TribeState& t = tribe(s, unit.tribe);
    check(disband_legal(s, u), "disband: illegal");
    const auto ui = static_cast<std::size_t>(unit.type);
    add_stars(t, kUnitCost[ui] / 2);
    t.score -= kUnitPoints[ui];
    remove_unit(s, u);
}

// Every other unit in u's neighbourhood of radius = u's range, x-major then y (02 §2 scan order).
template <typename F>
void for_each_unit_in_range(const GameState& s, Slot u, const F& f) {
    const Unit& unit = s.units[u];
    const int r = kUnitRange[static_cast<std::size_t>(unit.type)];
    for (int x = unit.x - r; x <= unit.x + r; ++x) {
        for (int y = unit.y - r; y <= unit.y + r; ++y) {
            const Slot t =
                s.on_board(x, y) ? s.unit_at[static_cast<std::size_t>(s.index(x, y))] : kNoSlot;
            if (t != kNoSlot && t != u) {
                f(t);
            }
        }
    }
}

// 03b §7: units of U's own tribe within U's range (centre excluded) that are below max HP.
template <typename F>
void for_each_heal_target(const GameState& s, Slot u, const F& f) {
    for_each_unit_in_range(s, u, [&](Slot t) {
        if (s.units[t].tribe == s.units[u].tribe && s.units[t].hp < s.units[t].max_hp) {
            f(t);
        }
    });
}

bool heal_others_legal(const GameState& s, Slot u) {
    const Unit& unit = s.units[u];
    if (unit.type != UnitType::MindBender || !can_attack(unit)) {
        return false;
    }
    bool any = false;
    for_each_heal_target(s, u, [&](Slot) { any = true; });
    return any;
}

void heal_others(GameState& s, Slot u) {
    check(heal_others_legal(s, u), "heal others: illegal");
    for_each_heal_target(s, u, [&](Slot t) {
        Unit& target = s.units[t];
        target.hp =
            static_cast<std::int16_t>(std::min(target.hp + kMindBenderHeal, int{target.max_hp}));
    });
    s.units[u].status = status_after_attack(s.units[u]);
}

// 03b §7. Generation also requires another tribe's target (the executor alone does not).
bool convert_legal(const GameState& s, Slot u, Slot t) {
    const Unit& unit = s.units[u];
    const Unit& target = s.units[t];
    return unit.type == UnitType::MindBender && can_attack(unit) && target.tribe != unit.tribe &&
           tribe(s, unit.tribe)
               .visible.test(static_cast<std::size_t>(s.index(target.x, target.y))) &&
           chebyshev_units(unit, target) <= kUnitRange[static_cast<std::size_t>(unit.type)];
}

// 03b §7: the target leaves its list and joins the active tribe as a tribe-owned unit; the
// diplomacy repercussion hits (P, P) because the target's tribe is already P.
void convert(GameState& s, Slot u, std::int32_t target_id) {
    const Slot t = unit_by_id(s, target_id);
    check(convert_legal(s, u, t), "convert: illegal");
    Unit& target = s.units[t];
    if (target.city != kNoSlot) {
        s.cities[target.city].units.erase_value(t);
    } else {
        tribe(s, target.tribe).tribe_units.erase_value(t);
    }
    const int p = s.units[u].tribe;
    target.tribe = static_cast<std::int16_t>(p);
    target.city = kNoSlot;
    tribe(s, s.active_tribe).tribe_units.push_back(t);
    update_allegiance(s, kAttackRepercussion, p, p);
    consequences(s, kAttackRepercussion, p, p);
    s.units[u].status = status_after_attack(s.units[u]);
    target.status = TurnStatus::Finished;
}

// Capitals mode (04 §8.2): the first tribe in id order, not lost, that owns every starting capital
// is set to Win. Returns whether there is one.
bool capitals_winner(GameState& s) {
    const auto n = static_cast<std::size_t>(s.num_tribes);
    for (std::size_t i = 0; i < n; ++i) {
        TribeState& t = s.tribes[i];
        if (t.result == Result::Loss) {
            continue;
        }
        bool all = true;
        for (std::size_t j = 0; j < n; ++j) {
            all = all && t.cities.contains(s.tribes[j].capital);
        }
        if (all) {
            t.result = Result::Win;
            return true;
        }
    }
    return false;
}

// Ranking (04 §8.3, TribeResult.compareTo), each key descending; the first gets Win, the rest Loss.
void rank_tribes(GameState& s) {
    struct Key {
        int win, score, techs, cities, production, wars, sent;
        auto operator<=>(const Key&) const = default;
    };
    const auto n = static_cast<std::size_t>(s.num_tribes);
    std::array<Key, kMaxTribes> key{};
    std::array<int, kMaxTribes> order{};
    for (std::size_t i = 0; i < n; ++i) {
        const TribeState& t = s.tribes[i];
        int prod = 0;
        for (Slot c : t.cities) {
            prod += production(s.cities[c]);  // no occupation penalty, no clamp
        }
        key[i] = {.win = t.result == Result::Win ? 1 : 0,
                  .score = t.score,
                  .techs = std::popcount(t.techs),
                  .cities = static_cast<int>(t.cities.size()),
                  .production = prod,
                  .wars = t.wars_declared,
                  .sent = t.stars_sent_total};
        order[i] = static_cast<int>(i);
    }
    const std::span<int> ranked(order.data(), n);
    std::ranges::stable_sort(ranked, [&](int a, int b) {
        return key[static_cast<std::size_t>(a)] > key[static_cast<std::size_t>(b)];
    });
    for (std::size_t r = 0; r < n; ++r) {
        s.tribes[static_cast<std::size_t>(ranked[r])].result = r == 0 ? Result::Win : Result::Loss;
    }
}

// ---- action generation (04 §5) ----

Action at_tile(ActionType type, std::int32_t actor, int x, int y, int param = 0) {
    return {.type = type,
            .actor = actor,
            .x = static_cast<std::uint8_t>(x),
            .y = static_cast<std::uint8_t>(y),
            .param = static_cast<std::uint8_t>(param)};
}

using CityTiles = InlineVector<std::uint16_t, kMaxCityBuildings>;

// 03b §2: city c's tiles for action generation, x-major then y.
CityTiles city_tiles(const GameState& s, Slot c) {
    const City& city = s.cities[c];
    const int r = city.level < 4 ? 1 : 2;
    CityTiles tiles;
    for (int x = city.x - r; x <= city.x + r; ++x) {
        for (int y = city.y - r; y <= city.y + r; ++y) {
            if (is_city_tile(s, c, x, y)) {
                tiles.push_back(static_cast<std::uint16_t>(s.index(x, y)));
            }
        }
    }
    return tiles;
}

// Legality of the one-variant-per-tile city actions (Burn / Clear / Grow forest, Destroy, Gather).
bool tile_action_legal(const GameState& s, Slot c, int x, int y, ActionType type) {
    switch (type) {
        case ActionType::Destroy:
            return destroy_legal(s, c, x, y);
        case ActionType::GatherResource:
            return gather_legal(s, c, x, y);
        default:
            return forest_legal(s, c, x, y, type);
    }
}

// City c's actions other than level-up, in Tribes' factory order; each factory scans the city
// tiles.
void city_actions(const GameState& s, Slot c, ActionList& out) {
    const City& city = s.cities[c];
    const CityTiles tiles = city_tiles(s, c);
    const auto each_tile = [&](const auto& f) {
        for (const std::uint16_t i : tiles) {
            f(i / s.side, i % s.side);
        }
    };
    each_tile([&](int x, int y) {
        for (int b = 0; b < static_cast<int>(Building::None); ++b) {
            if (build_legal(s, c, x, y, static_cast<Building>(b))) {
                out.push_back(at_tile(ActionType::Build, city.id, x, y, b));
            }
        }
    });
    for (const ActionType type :
         {ActionType::BurnForest, ActionType::ClearForest, ActionType::Destroy,
          ActionType::GrowForest, ActionType::GatherResource}) {
        each_tile([&](int x, int y) {
            if (tile_action_legal(s, c, x, y, type)) {
                const int param = type == ActionType::GatherResource
                                      ? static_cast<int>(s.resource[s.index(x, y)])
                                      : 0;
                out.push_back(at_tile(type, city.id, x, y, param));
            }
        });
    }
    for (int u = 0; u <= static_cast<int>(UnitType::MindBender); ++u) {
        if (spawn_legal(s, c, static_cast<UnitType>(u))) {
            out.push_back({.type = ActionType::Spawn,
                           .actor = city.id,
                           .param = static_cast<std::uint8_t>(u)});
        }
    }
}

// Unit u's actions in Tribes' factory order (04 §5).
void unit_actions(const GameState& s, Slot u, ActionList& out) {
    const Unit& unit = s.units[u];
    const auto simple = [&](ActionType type) { out.push_back({.type = type, .actor = unit.id}); };
    if (upgrade_legal(s, u)) {
        simple(ActionType::Upgrade);
    }
    if (unit.status == TurnStatus::Finished) {
        return;
    }
    for_each_unit_in_range(s, u, [&](Slot t) {
        if (attack_legal(s, u, t)) {
            out.push_back({.type = ActionType::Attack, .actor = unit.id, .target = s.units[t].id});
        }
    });
    if (const auto target = capture_target(s, u)) {
        out.push_back({.type = ActionType::Capture, .actor = unit.id, .target = *target});
    }
    for_each_unit_in_range(s, u, [&](Slot t) {
        if (convert_legal(s, u, t)) {
            out.push_back({.type = ActionType::Convert, .actor = unit.id, .target = s.units[t].id});
        }
    });
    if (disband_legal(s, u)) {
        simple(ActionType::Disband);
    }
    if (examine_legal(s, u)) {
        simple(ActionType::Examine);
    }
    if (heal_others_legal(s, u)) {
        simple(ActionType::HealOthers);
    }
    if (make_veteran_legal(unit)) {
        simple(ActionType::MakeVeteran);
    }
    if (can_move(unit)) {
        MoveTargets targets;
        move_targets(s, u, targets);
        for (int k = 0; k < targets.count; ++k) {
            const int i = targets.tiles[static_cast<std::size_t>(k)];
            out.push_back(at_tile(ActionType::Move, unit.id, i / s.side, i % s.side));
        }
    }
    if (can_recover(s, u)) {
        simple(ActionType::Recover);
    }
}

// Tribe p's actions (04 §5 step 3); End turn is added by the caller.
void tribe_actions(const GameState& s, int p, ActionList& out) {
    for (int x = 0; x < s.side; ++x) {
        for (int y = 0; y < s.side; ++y) {
            if (road_legal(s, p, x, y)) {
                out.push_back(at_tile(ActionType::BuildRoad, p, x, y));
            }
        }
    }
    for (int k = 0; k < kNumTechs; ++k) {
        if (research_legal(tribe(s, p), static_cast<Tech>(k))) {
            out.push_back({.type = ActionType::ResearchTech,
                           .actor = p,
                           .param = static_cast<std::uint8_t>(k)});
        }
    }
    for (int q = 0; q < s.num_tribes; ++q) {
        if (declare_war_legal(s, p, q)) {
            out.push_back({.type = ActionType::DeclareWar, .actor = p, .target = q});
        }
    }
    for (int q = 0; q < s.num_tribes; ++q) {
        for (int n = 1; n < kSendStarsBound; ++n) {
            if (send_stars_legal(s, p, q, n)) {
                out.push_back({.type = ActionType::SendStars,
                               .actor = p,
                               .target = q,
                               .amount = static_cast<std::int16_t>(n)});
            }
        }
    }
}

}  // namespace

void legal_actions(const GameState& s, ActionList& out) {
    out.clear();
    if (s.game_over) {
        return;
    }
    const int p = s.active_tribe;
    for (Slot c : tribe(s, p).cities) {
        const City& city = s.cities[c];
        if (can_level_up(city)) {  // only this city's level-up variants, no End turn
            out.clear();
            const int first = 2 * level_up_pair(city);
            for (int b = first; b < first + 2; ++b) {
                out.push_back({.type = ActionType::LevelUp,
                               .actor = city.id,
                               .param = static_cast<std::uint8_t>(b)});
            }
            return;
        }
        city_actions(s, c, out);
    }
    for_each_unit_of(s, p, [&](Slot u) { unit_actions(s, u, out); });
    tribe_actions(s, p, out);
    out.push_back({.type = ActionType::EndTurn, .actor = p});
}

Slot city_by_id(const GameState& s, std::int32_t id) {
    for (std::size_t i = 0; i < s.cities.size(); ++i) {
        if (id != 0 && s.cities[i].id == id) {
            return static_cast<Slot>(i);
        }
    }
    check(false, "no city with this actor id");
    return kNoSlot;
}

Slot unit_by_id(const GameState& s, std::int32_t id) {
    for (std::size_t i = 0; i < s.units.size(); ++i) {
        if (id != 0 && s.units[i].id == id) {
            return static_cast<Slot>(i);
        }
    }
    check(false, "no unit with this actor id");
    return kNoSlot;
}

bool update_game_over(GameState& s) {
    bool ended = false;
    if (s.mode == GameMode::Capitals) {
        ended = capitals_winner(s);
    } else {
        int not_lost = 0;
        for (int i = 0; i < s.num_tribes; ++i) {
            not_lost += tribe(s, i).result != Result::Loss ? 1 : 0;
        }
        ended = not_lost <= 1;
    }
    const int max_turns = s.mode == GameMode::Capitals ? kMaxTurnsCapitals : kMaxTurnsScore;
    if (ended || s.tick > max_turns) {
        rank_tribes(s);
        ended = true;
    }
    s.game_over = ended;
    return ended;
}

int next_turn_tribe(GameState& s) {
    for (int step = 0; step < 2 * s.num_tribes; ++step) {
        int p = s.active_tribe + 1;
        if (p >= s.num_tribes) {
            p = 0;
            ++s.tick;
        }
        s.active_tribe = static_cast<std::int16_t>(p);
        if (tribe(s, p).result == Result::Incomplete) {
            return p;
        }
    }
    check(false, "next turn: no tribe is still playing");
    return -1;
}

void start_turn(GameState& s, int p) {
    s.active_tribe = static_cast<std::int16_t>(p);
    s.turn_must_end = false;
    TribeState& t = tribe(s, p);
    int income = 0;
    for (Slot c : t.cities) {
        City& city = s.cities[c];
        const Slot occupant = s.unit_at[static_cast<std::size_t>(s.index(city.x, city.y))];
        if (occupant == kNoSlot || s.units[occupant].tribe == p) {
            income += production(city);
        }
        for (BuildingEntry& b : city.buildings) {
            if (is_temple(b.type)) {
                const int points = tick_temple(b);
                t.score += points;
                city.points_worth += points;
            }
        }
    }
    if (s.tick == 0) {
        t.stars = kStartingStars;
    } else {
        add_stars(t, std::max(0, income));
    }
    for_each_unit_of(s, p, [&](Slot u) {
        Unit& unit = s.units[u];
        unit.status = unit.status == TurnStatus::Pushed ? TurnStatus::Moved : TurnStatus::Fresh;
    });
    if (t.has_tech(Tech::Meditation) && ++t.pacifist_count == kPacifistTurns) {
        monument(t, Building::AltarOfPeace) = MonumentStatus::Available;  // 03a §5 quirk
    }
    t.stars_sent_this_turn = 0;
    t.declared_war_this_turn = false;
    refresh_action_flags(s);
}

void end_turn(GameState& s) {
    for_each_unit_of(s, s.active_tribe, [&](Slot u) {
        if (can_recover(s, u)) {
            recover(s, u);
        }
    });
}

void apply_action(GameState& s, const Action& a, Chance& chance) {
    const int p = s.active_tribe;
    switch (a.type) {
        case ActionType::EndTurn:
            check(false, "apply_action: End turn is handled by end_turn");
            break;
        case ActionType::ResearchTech:
            check(a.actor == p, "research: not the active tribe");
            research(s, p, static_cast<Tech>(a.param));
            break;
        case ActionType::DeclareWar:
            check(a.actor == p, "declare war: not the active tribe");
            declare_war(s, p, a.target);
            break;
        case ActionType::SendStars:
            check(a.actor == p, "send stars: not the active tribe");
            send_stars(s, p, a.target, a.amount);
            break;
        case ActionType::BuildRoad:
            check(a.actor == p, "build road: not the active tribe");
            build_road(s, p, a.x, a.y);
            break;
        case ActionType::GatherResource:
        case ActionType::LevelUp:
        case ActionType::Spawn:
        case ActionType::ClearForest:
        case ActionType::GrowForest:
        case ActionType::BurnForest:
        case ActionType::Build:
        case ActionType::Destroy: {
            const Slot c = city_by_id(s, a.actor);
            check(s.cities[c].tribe == p, "city action: not the active tribe's city");
            if (a.type == ActionType::GatherResource) {
                gather(s, c, a.x, a.y, static_cast<Resource>(a.param));
            } else if (a.type == ActionType::LevelUp) {
                level_up(s, c, static_cast<LevelUpBonus>(a.param), chance);
            } else if (a.type == ActionType::Spawn) {
                spawn(s, c, static_cast<UnitType>(a.param));
            } else if (a.type == ActionType::Build) {
                build(s, c, a.x, a.y, static_cast<Building>(a.param));
            } else if (a.type == ActionType::Destroy) {
                destroy(s, c, a.x, a.y);
            } else {
                forest_action(s, c, a.x, a.y, a.type);
            }
            break;
        }
        case ActionType::Recover:
        case ActionType::Disband:
        case ActionType::Move:
        case ActionType::Examine:
        case ActionType::Capture:
        case ActionType::Attack:
        case ActionType::Upgrade:
        case ActionType::MakeVeteran:
        case ActionType::HealOthers:
        case ActionType::Convert: {
            const Slot u = unit_by_id(s, a.actor);
            check(s.units[u].tribe == p, "unit action: not the active tribe's unit");
            if (a.type == ActionType::Recover) {
                recover(s, u);
            } else if (a.type == ActionType::Disband) {
                disband(s, u);
            } else if (a.type == ActionType::Examine) {
                examine(s, u, chance);
            } else if (a.type == ActionType::Capture) {
                capture(s, u, a.target, chance);
            } else if (a.type == ActionType::Attack) {
                attack(s, u, a.target);
            } else if (a.type == ActionType::Upgrade) {
                upgrade(s, u);
            } else if (a.type == ActionType::MakeVeteran) {
                make_veteran(s, u);
            } else if (a.type == ActionType::HealOthers) {
                heal_others(s, u);
            } else if (a.type == ActionType::Convert) {
                convert(s, u, a.target);
            } else {
                move(s, u, a.x, a.y);
            }
            break;
        }
    }
    refresh_action_flags(s);
}

}  // namespace poly
