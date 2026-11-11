// Determinization (ADR-009). Expected behaviour comes from the ADR: techs implied by what the
// observer sees (with parents) plus random ones up to 1 + ⌊tick/2⌋, population below the city's
// need, stars = income × {0, 1, 2}, nothing the observation shows changed, and the leak property:
// true states that differ only in hidden information determinize identically.

#include <gtest/gtest.h>

#include <bit>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/dump.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/determinize.hpp"

namespace {

using poly::GameState;
using poly::Slot;
using poly::Tech;

GameState board(poly::ObsMode mode) {  // 11 x 11 plains, tribe keys 0 and 2, tribe 0 to move
    std::vector<std::string> lines(11, ".,.,.,.,.,.,.,.,.,.,.");
    lines[2] = ".,.,c:0,.,.,.,.,.,.,.,.";
    lines[8] = ".,.,.,.,.,.,.,.,c:2,.,.";
    poly::SampledChance chance(1);
    GameState s = poly::load_level(lines, poly::GameMode::Capitals, mode, chance);
    poly::start_turn(s, 0);
    return s;
}

Slot place(GameState& s, int t, poly::UnitType type, int x, int y) {
    const Slot u = poly::new_unit(s);
    poly::Unit& unit = s.units[u];
    unit.type = type;
    unit.tribe = static_cast<std::int16_t>(t);
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    unit.hp = unit.max_hp = 10;
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
    s.tribes[static_cast<std::size_t>(t)].tribe_units.push_back(u);
    return u;
}

GameState det(const GameState& truth, std::uint64_t seed) {
    poly::Rng rng(seed);
    poly::SampledChance chance(seed + 1);
    return poly::search::determinize(poly::observe(truth, 0), rng, chance);
}

TEST(Determinize, EvidenceImpliesTechsWithParents) {
    GameState s = board(poly::ObsMode::Full);
    place(s, 1, poly::UnitType::Knight, 7, 7);  // Chivalry ← Free Spirit ← Riding
    const auto& cap = s.cities[s.tribes[1].capital];
    s.building[static_cast<std::size_t>(s.index(cap.x + 1, cap.y))] = poly::Building::Mine;
    ASSERT_EQ(s.city_owner[static_cast<std::size_t>(s.index(cap.x + 1, cap.y))],
              s.tribes[1].capital);
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        const GameState d = det(s, seed);
        const poly::TribeState& t = d.tribes[1];
        for (const Tech k :
             {Tech::Chivalry, Tech::FreeSpirit, Tech::Riding, Tech::Mining, Tech::Climbing}) {
            EXPECT_TRUE(t.has_tech(k)) << "seed " << seed << " tech " << static_cast<int>(k);
        }
        if (const auto start = poly::kStartTech[static_cast<std::size_t>(t.type)]) {
            EXPECT_TRUE(t.has_tech(*start));
        }
    }
}

TEST(Determinize, OnlyTheirOwnBuildingsCount) {
    // At tick 0 the target is the starting tech alone, so any other tech is evidence: a forge on
    // the observer's own land says nothing about the opponent.
    GameState s = board(poly::ObsMode::Full);
    const auto& mine = s.cities[s.tribes[0].capital];
    const auto i = static_cast<std::size_t>(s.index(mine.x + 1, mine.y));
    s.building[i] = poly::Building::Forge;
    ASSERT_EQ(s.city_owner[i], s.tribes[0].capital);
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        EXPECT_FALSE(det(s, seed).tribes[1].has_tech(Tech::Smithery)) << "seed " << seed;
    }
}

TEST(Determinize, TechCountFollowsTheTick) {  // 1 + ⌊tick/2⌋, random beyond the evidence
    GameState s = board(poly::ObsMode::Full);
    s.tick = 9;
    std::set<std::uint32_t> seen;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const GameState d = det(s, seed);
        EXPECT_EQ(std::popcount(d.tribes[1].techs), 1 + (9 / 2)) << "seed " << seed;
        seen.insert(d.tribes[1].techs);
    }
    EXPECT_GT(seen.size(), 1U);  // drawn, not fixed
}

// Checks one determinization's cities and stars; returns its number of saved turns (stars /
// income).
int saved_turns(const GameState& d) {
    int income = 0;
    for (const Slot c : d.tribes[1].cities) {
        const poly::City& city = d.cities[c];
        EXPECT_GE(city.population, 0);
        EXPECT_LT(city.population, std::max<int>(city.population_need, 1));
        income += poly::city_production(city);
    }
    EXPECT_GT(income, 0);
    if (income <= 0) {
        return -1;
    }
    EXPECT_EQ(d.tribes[1].stars % income, 0);
    return d.tribes[1].stars / income;
}

TEST(Determinize, CityAndStarBounds) {
    const GameState s = board(poly::ObsMode::Full);
    std::set<int> turns;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        turns.insert(saved_turns(det(s, seed)));
    }
    EXPECT_EQ(turns, (std::set<int>{0, 1, 2}));  // within {0, 1, 2}, and every value occurs
}

// Undo exactly what determinize may change; what's left must be the observation itself.
GameState undo_hidden(GameState d, const GameState& view, int observer) {
    for (int q = 0; q < d.num_tribes; ++q) {
        if (q == observer) {
            continue;
        }
        auto& t = d.tribes[static_cast<std::size_t>(q)];
        const auto& v = view.tribes[static_cast<std::size_t>(q)];
        t.techs = v.techs;
        t.all_researched = v.all_researched;
        t.stars = v.stars;
        for (const Slot c : t.cities) {
            d.cities[c].population = view.cities[c].population;
        }
    }
    return d;
}

TEST(Determinize, ChangesNothingTheObservationShows) {
    for (const poly::ObsMode mode : {poly::ObsMode::Full, poly::ObsMode::Partial}) {
        GameState s = board(mode);
        s.tick = 6;
        place(s, 1, poly::UnitType::Archer, 7, 7);
        const GameState view = poly::observe(s, 0).view();
        const GameState d = det(s, 3);
        EXPECT_EQ(poly::dump_state(undo_hidden(d, view, 0)), poly::dump_state(view));
        EXPECT_NE(poly::dump_state(d), poly::dump_state(view));  // and it did change something
    }
}

TEST(Determinize, HiddenInformationDoesNotChangeIt) {  // the leak property
    for (const poly::ObsMode mode : {poly::ObsMode::Full, poly::ObsMode::Partial}) {
        GameState a = board(mode);
        a.tick = 6;
        GameState b = a;
        poly::TribeState& t = b.tribes[1];
        t.stars += 9;
        t.kills = 4;
        poly::mark_researched(t, Tech::Sailing);
        poly::mark_researched(t, Tech::Fishing);
        b.cities[t.capital].population += 1;
        b.cities[t.capital].production_bonus += 2;
        ASSERT_EQ(poly::dump_observation(poly::observe(a, 0)),
                  poly::dump_observation(poly::observe(b, 0)));
        EXPECT_EQ(poly::dump_state(det(a, 5)), poly::dump_state(det(b, 5)));
    }
}

}  // namespace
