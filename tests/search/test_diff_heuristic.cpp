// Diff heuristic (agents sheet 01 §4). Each expected value is the sheet's weight times the change
// the test makes, not a value read off our code.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/diff_heuristic.hpp"

namespace {

using poly::GameState;
using poly::Slot;
using poly::search::diff_heuristic;

// 11 x 11 plains with a capital per entry of `cities` ({x, y, tribe key}); tribe 0 to move.
GameState board(const std::vector<std::pair<std::pair<int, int>, int>>& cities, poly::ObsMode obs) {
    std::vector<std::vector<std::string>> grid(11, std::vector<std::string>(11, "."));
    for (const auto& [xy, key] : cities) {
        grid[static_cast<std::size_t>(xy.first)][static_cast<std::size_t>(xy.second)] =
            "c:" + std::to_string(key);
    }
    std::vector<std::string> lines;
    for (const auto& row : grid) {
        std::string l;
        for (std::size_t i = 0; i < row.size(); ++i) {
            l += (i > 0 ? "," : "") + row[i];
        }
        lines.push_back(l);
    }
    poly::SampledChance chance(1);
    GameState s = poly::load_level(lines, poly::GameMode::Capitals, obs, chance);
    poly::start_turn(s, 0);
    return s;
}

GameState two_tribes(poly::ObsMode obs = poly::ObsMode::Full) {
    return board({{{2, 2}, 0}, {{8, 8}, 2}}, obs);
}

poly::TribeState& tribe(GameState& s, int t) { return s.tribes[static_cast<std::size_t>(t)]; }
poly::City& capital(GameState& s, int t) { return s.cities[tribe(s, t).capital]; }

Slot add_unit(GameState& s, int t, int x, int y) {  // tribe-owned unless attached by the caller
    const Slot u = poly::new_unit(s);
    poly::Unit& unit = s.units[u];
    unit.type = poly::UnitType::Warrior;
    unit.tribe = static_cast<std::int16_t>(t);
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    unit.hp = unit.max_hp = 10;
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
    return u;
}

TEST(DiffHeuristic, UnchangedStateIsZero) {
    const GameState s = two_tribes();
    EXPECT_DOUBLE_EQ(diff_heuristic(s, s, 0), 0.0);
}

TEST(DiffHeuristic, OwnTermWeights) {
    const GameState ref = two_tribes();
    {
        GameState s = ref;
        tribe(s, 0).score += 30;
        EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 3.0);  // 0.1 per score point
    }
    {
        GameState s = ref;
        tribe(s, 0).techs |= 1U << 5U;
        EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 4.0);  // 4 per tech
    }
    {
        GameState s = ref;
        tribe(s, 0).kills += 2;
        EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 6.0);  // 3 per kill
    }
    {
        GameState s = ref;
        capital(s, 0).level += 1;  // +1 production and +1 level
        EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 5.0 + 2.0);
    }
    {
        GameState s = ref;
        tribe(s, 0).connected.push_back(tribe(s, 0).capital);
        EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 1.0);  // connected cities, unweighted
    }
}

TEST(DiffHeuristic, NewCityCountsCityLevelAndProduction) {
    const GameState ref = two_tribes();
    GameState s = ref;
    const Slot c = poly::new_city(s);
    s.cities[c].tribe = 0;
    s.cities[c].x = 5;
    s.cities[c].y = 2;
    s.cities[c].level = 1;  // production 1 (not a capital)
    tribe(s, 0).cities.push_back(c);
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 4.0 + 2.0 + 5.0);
}

TEST(DiffHeuristic, ProductionIsRaw) {  // 01 §4.1: no clamp at 0
    const GameState ref = two_tribes();
    const int before = poly::city_production(ref.cities[ref.tribes[0].capital]);
    ASSERT_EQ(before, 2);  // level 1 + capital bonus 1
    GameState s = ref;
    capital(s, 0).production_bonus = -4;  // production 2 -> -2
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 5.0 * -4);
    GameState n = ref;
    capital(n, 0).population = -1;  // negative population is the production (03a §1): 2 -> -1
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, n, 0), 5.0 * -3);
}

TEST(DiffHeuristic, OnlyUnitsWithAHomeCityCount) {
    const GameState ref = two_tribes();
    GameState s = ref;
    const Slot free = add_unit(s, 0, 3, 3);
    tribe(s, 0).tribe_units.push_back(free);
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 0.0);  // tribe-owned: not counted
    const Slot homed = add_unit(s, 0, 4, 4);
    s.units[homed].city = tribe(s, 0).capital;
    capital(s, 0).units.push_back(homed);
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 2.0);
}

TEST(DiffHeuristic, OpponentDeltaIsSubtracted) {
    const GameState ref = two_tribes();
    GameState s = ref;
    tribe(s, 1).techs |= 1U << 3U;
    tribe(s, 1).score += 10;
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), -(4.0 + 1.0));
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 1), 4.0 + 1.0);  // same change, other point of view
}

TEST(DiffHeuristic, OpponentsAreAveraged) {
    const GameState ref = board({{{2, 2}, 0}, {{8, 8}, 2}, {{2, 8}, 1}}, poly::ObsMode::Full);
    ASSERT_EQ(ref.num_tribes, 3);
    GameState s = ref;
    tribe(s, 1).kills += 1;  // d_1 = 3, d_2 = 0
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), -1.5);
    tribe(s, 2).kills += 1;  // d_1 = d_2 = 3
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), -3.0);
}

TEST(DiffHeuristic, VisionIsTheActiveTribes) {  // 01 §4.2 quirk
    const GameState ref = two_tribes(poly::ObsMode::Partial);
    const auto seen = [](const GameState& g, int t) {
        return static_cast<double>(g.tribes[static_cast<std::size_t>(t)].visible.count());
    };
    GameState s = ref;
    tribe(s, 0).visible.set(static_cast<std::size_t>(s.index(10, 10)));  // P (active) sees more
    ASSERT_EQ(seen(s, 0), seen(ref, 0) + 1);
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 1.0);
    s.active_tribe = 1;  // now the opponent's grid is compared with P's in ref
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), seen(s, 1) - seen(ref, 0));
}

TEST(DiffHeuristic, PartialObsOpponentTurnJumpsToTheWholeMap) {  // 01 §4.3
    const GameState truth = two_tribes(poly::ObsMode::Partial);
    const GameState ref = poly::observe(truth, 0).view();
    const auto mine = static_cast<double>(ref.tribes[0].visible.count());
    ASSERT_LT(mine, 121.0);
    GameState s = ref;
    s.active_tribe = 1;  // as after End Turn in the search tree
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 121.0 - mine);
}

TEST(DiffHeuristic, FullObsVisionTermIsZero) {  // 01 §4.3
    const GameState ref = poly::observe(two_tribes(), 0).view();
    GameState s = ref;
    s.active_tribe = 1;
    EXPECT_DOUBLE_EQ(diff_heuristic(ref, s, 0), 0.0);
}

}  // namespace
