#include "poly/level.hpp"

#include <stdexcept>
#include <string_view>

namespace poly {

namespace {

struct Cell {
    char terrain = 0;
    std::string_view extra;  // text after ':', may be empty
};

std::vector<Cell> split_line(std::string_view line) {
    std::vector<Cell> cells;
    std::size_t start = 0;
    while (start <= line.size()) {
        std::size_t end = line.find(',', start);
        if (end == std::string_view::npos) {
            end = line.size();
        }
        const std::string_view tok = line.substr(start, end - start);
        if (tok.empty()) {
            throw std::invalid_argument("level: empty cell");
        }
        Cell c;
        c.terrain = tok[0];
        if (tok.size() > 1) {
            if (tok[1] != ':') {
                throw std::invalid_argument("level: cell must be terrain[:extra]");
            }
            c.extra = tok.substr(2);
        }
        cells.push_back(c);
        start = end + 1;
    }
    return cells;
}

Terrain parse_terrain(char ch) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(Terrain::Fog); ++i) {
        if (kTerrainChar[i] == ch) {
            return static_cast<Terrain>(i);
        }
    }
    throw std::invalid_argument(std::string("level: unknown terrain '") + ch + "'");
}

// A non-city cell's extra: first char is a resource char, anything else means no resource (05
// §1.1).
Resource parse_resource(std::string_view extra) {
    if (extra.empty()) {
        return Resource::None;
    }
    for (std::size_t i = 0; i < kResourceChar.size(); ++i) {
        if (kResourceChar[i] == extra[0]) {
            return static_cast<Resource>(i);
        }
    }
    return Resource::None;
}

TribeType parse_tribe_key(std::string_view extra) {
    int key = 0;
    bool ok = !extra.empty() && extra.size() <= 2;
    for (const char ch : extra) {
        ok = ok && ch >= '0' && ch <= '9';
        key = (key * 10) + (ch - '0');
    }
    if (!ok || key >= kNumTribeTypes) {
        throw std::invalid_argument("level: city cell needs a tribe key 0..11");
    }
    return static_cast<TribeType>(key);
}

void level_up(City& c) {  // 03a §1
    c.level += 1;
    c.population = static_cast<std::int16_t>(c.population - c.population_need);
    c.population_need = static_cast<std::int16_t>(c.level + 1);
}

using Grid = std::vector<std::vector<Cell>>;

Grid parse_grid(const std::vector<std::string>& lines) {
    if (lines.size() < 5 || lines.size() > static_cast<std::size_t>(kMaxSide)) {
        throw std::invalid_argument("level: side must be 5.." + std::to_string(kMaxSide));
    }
    Grid grid;
    grid.reserve(lines.size());
    for (const std::string& l : lines) {
        grid.push_back(split_line(l));
        if (grid.back().size() != lines.size()) {
            throw std::invalid_argument("level: every line needs exactly n cells");
        }
    }
    return grid;
}

void init_tribe(TribeState& t, TribeType type, ObsMode obs, std::size_t side) {
    t.type = type;
    if (const auto tech = kStartTech[static_cast<std::size_t>(type)]) {
        t.techs |= 1U << static_cast<unsigned>(*tech);
        t.score += kTechTier[static_cast<std::size_t>(*tech)] * 100;  // 01: tier x 100
    }
    if (obs == ObsMode::Full) {
        for (std::size_t i = 0; i < side * side; ++i) {
            t.visible.set(i);
        }
    }
}

// Pass 1: tribes, one per city cell, ids in row-major order (05 §1.2).
void add_tribes(GameState& s, const Grid& grid) {
    int count = 0;
    for (const auto& row : grid) {
        for (const Cell& c : row) {
            if (c.terrain != kTerrainChar[static_cast<std::size_t>(Terrain::City)]) {
                continue;
            }
            const TribeType type = parse_tribe_key(c.extra);
            for (int t = 0; t < count; ++t) {
                if (s.tribes[static_cast<std::size_t>(t)].type == type) {
                    throw std::invalid_argument("level: duplicate tribe type");
                }
            }
            if (count == kMaxTribes) {
                throw std::invalid_argument("level: more than 8 tribes");
            }
            init_tribe(s.tribes[static_cast<std::size_t>(count++)], type, s.obs, grid.size());
        }
    }
    if (count < 2) {
        throw std::invalid_argument("level: needs at least 2 cities");
    }
    s.num_tribes = static_cast<std::int16_t>(count);
}

// One capital, set up in the order of 05 §1.3.
void add_capital(GameState& s, int tribe, int x, int y, Chance& chance) {
    TribeState& t = s.tribes[static_cast<std::size_t>(tribe)];
    const auto i = static_cast<std::size_t>(s.index(x, y));

    const Slot cs = new_city(s);  // 1. capital, level 1
    City& city = s.cities[cs];
    city.tribe = static_cast<std::int16_t>(tribe);
    city.x = static_cast<std::uint8_t>(x);
    city.y = static_cast<std::uint8_t>(y);
    city.capital = true;
    if (t.type == TribeType::Luxidoor) {  // 2. level 3, walls, population 0
        level_up(city);
        level_up(city);
        city.population = 0;
        city.walls = true;
    }
    t.capital = cs;  // 3. add city: capital, city list, clear view r1, network flag
    t.cities.push_back(cs);
    (void)clear_view(s, tribe, x, y, 1, chance);  // new city: no recomputation (03b §1)
    s.network.set(i);
    t.score += kCityCentrePoints;  // 4.

    const UnitType ut = kStartUnit[static_cast<std::size_t>(t.type)];  // 5. starting unit
    const Slot us = new_unit(s);
    Unit& u = s.units[us];
    u.type = ut;
    u.tribe = static_cast<std::int16_t>(tribe);
    u.city = cs;
    u.x = city.x;
    u.y = city.y;
    u.hp = u.max_hp = static_cast<std::int16_t>(kUnitMaxHp[static_cast<std::size_t>(ut)]);
    u.status = TurnStatus::Finished;  // created units start Finished (02)
    s.unit_at[i] = us;
    city.units.push_back(us);
    t.score += kUnitPoints[static_cast<std::size_t>(ut)];

    claim_tiles(s, cs, city.border_radius);  // 6.
    s.terrain[i] = Terrain::City;            // 7. (city cells carry no resource)
}

}  // namespace

GameState load_level(const std::vector<std::string>& lines, GameMode mode, ObsMode obs,
                     Chance& chance) {
    const Grid grid = parse_grid(lines);
    const auto n = static_cast<int>(grid.size());

    GameState s;
    s.side = static_cast<std::int16_t>(n);
    s.mode = mode;
    s.obs = obs;
    s.unit_at.fill(kNoSlot);
    s.city_owner.fill(kNoSlot);
    s.terrain.fill(Terrain::DeepWater);
    s.resource.fill(Resource::None);
    s.building.fill(Building::None);

    add_tribes(s, grid);

    // Pass 2: fill the board in row-major order.
    int next_tribe = 0;
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y) {
            const Cell& c = grid[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
            const Terrain terrain = parse_terrain(c.terrain);
            if (terrain == Terrain::City) {
                add_capital(s, next_tribe++, x, y, chance);
            } else {
                const auto i = static_cast<std::size_t>(s.index(x, y));
                s.terrain[i] = terrain;
                s.resource[i] = parse_resource(c.extra);
            }
        }
    }

    // Game start: clear view radius 2 around each tribe's first city, in tribe-id order (04 §1.1).
    for (int t = 0; t < s.num_tribes; ++t) {
        const City& c = s.cities[s.tribes[static_cast<std::size_t>(t)].cities[0]];
        (void)clear_view(s, t, c.x, c.y, 2, chance);  // GameState.init: no recomputation
    }
    return s;
}

}  // namespace poly
