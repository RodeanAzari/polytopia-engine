// Tribes' agent-side forward model (rule facts 04 §9), the one its search agents simulate. Each
// test pins one way it differs from the game loop (04 §1–§2); expected values come from the rule
// sheet. The probe difftest (oracle_diff, PROBE records) checks it against Tribes on real
// observations.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/dump.hpp"
#include "poly/level.hpp"
#include "poly/rules.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::Slot;

// An n x n plain level with a capital "c:<tribe type>" at each given cell.
GameState level_with_capitals(const std::vector<std::pair<std::pair<int, int>, int>>& capitals) {
    constexpr int kN = 11;
    std::vector<std::vector<std::string>> grid(kN, std::vector<std::string>(kN, "."));
    for (const auto& [pos, type] : capitals) {
        grid[static_cast<std::size_t>(pos.first)][static_cast<std::size_t>(pos.second)] =
            "c:" + std::to_string(type);
    }
    std::vector<std::string> lines;
    for (const auto& row : grid) {
        std::string l;
        for (std::size_t i = 0; i < row.size(); ++i) {
            l += (i > 0 ? "," : "") + row[i];
        }
        lines.push_back(l);
    }
    poly::SampledChance chance(1);  // full obs: never drawn at load
    return poly::load_level(lines, poly::GameMode::Capitals, poly::ObsMode::Full, chance);
}

GameState two_tribes() { return level_with_capitals({{{2, 2}, 0}, {{8, 8}, 2}}); }

const Action kEndTurn{};  // type EndTurn

void advance(GameState& s, const Action& a) {
    poly::SampledChance chance(7);
    poly::agent_advance(s, a, chance);
}

Slot capital_unit(const GameState& s, int tribe) {
    return s.cities[s.tribes[static_cast<std::size_t>(tribe)].capital].units[0];
}

void teleport(GameState& s, Slot u, int x, int y) {  // test setup only; no rules applied
    poly::Unit& unit = s.units[u];
    s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = poly::kNoSlot;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
}

// Xin Xi (tribe 0, active) captures Bardur's capital, Bardur's only city, via agent_advance.
void capture_enemy_capital(GameState& s) {
    teleport(s, capital_unit(s, 1), 9, 8);
    teleport(s, capital_unit(s, 0), 8, 8);
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    const auto* const capture = std::ranges::find(legal, ActionType::Capture, &Action::type);
    ASSERT_NE(capture, legal.end());
    advance(s, *capture);
}

// 04 §9 step 4: End turn passes play on but never increases the tick, even when it wraps past the
// last tribe id (the game loop would: 04 §1). Step 5 clears the turn-ending mark.
TEST(AgentAdvance, EndTurnNeverIncreasesTheTick) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    advance(s, kEndTurn);
    EXPECT_EQ(s.active_tribe, 1);
    EXPECT_EQ(s.tick, 0);
    EXPECT_FALSE(s.turn_must_end);
    advance(s, kEndTurn);  // wraps 1 -> 0
    EXPECT_EQ(s.active_tribe, 0);
    EXPECT_EQ(s.tick, 0);
    EXPECT_FALSE(s.game_over);
}

// 04 §9: End turn doesn't execute while a level-up is pending; nothing changes at all.
TEST(AgentAdvance, EndTurnIgnoredWhileLevelUpPending) {
    GameState s = two_tribes();
    s.cities[s.tribes[0].capital].population = 2;  // level 1 needs 2 (04 §5: level-up only)
    poly::start_turn(s, 0);
    ASSERT_TRUE(s.leveling_up);
    const std::string before = poly::dump_state(s);
    poly::SampledChance chance(7);
    EXPECT_FALSE(poly::agent_advance(s, kEndTurn, chance));
    EXPECT_EQ(poly::dump_state(s), before);
}

// 04 §9: no game-over check after a non-End-turn action: capturing the last capital leaves the game
// running; the End turn that follows ends it (Xin Xi wins) and starts no further turn.
TEST(AgentAdvance, CaptureEndsTheGameOnlyAtEndTurn) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    capture_enemy_capital(s);
    EXPECT_FALSE(s.game_over);
    EXPECT_EQ(s.tribes[0].result, poly::Result::Incomplete);

    advance(s, kEndTurn);
    EXPECT_TRUE(s.game_over);
    EXPECT_EQ(s.tribes[0].result, poly::Result::Win);
    EXPECT_EQ(s.active_tribe, 0);  // no turn start for tribe 1
    EXPECT_TRUE(s.turn_must_end);  // step 1's mark stays: only turn start (step 5) clears it
}

// 04 §9: an action that isn't legal in the copy (e.g. an environment action whose resource the
// observation masks) executes nothing and reports it, so the caller keeps its stale legal list.
TEST(AgentAdvance, IllegalActionIsANoOp) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    s.tribes[0].stars = 10;
    s.tribes[0].techs |= 1U << static_cast<unsigned>(poly::Tech::Farming);
    const Action farm{.type = ActionType::Build,
                      .actor = s.cities[s.tribes[0].capital].id,
                      .x = 2,
                      .y = 3,  // in the capital's borders, no crops: Farm not legal
                      .param = static_cast<std::uint8_t>(poly::Building::Farm)};
    const std::string before = poly::dump_state(s);
    poly::SampledChance chance(7);
    EXPECT_FALSE(poly::agent_advance(s, farm, chance));
    EXPECT_EQ(poly::dump_state(s), before);
}

// 03b §3 step 8: capturing a tribe's last city eliminates it only in the native state; an agent's
// copy (observation) keeps it in the game.
poly::Result result_after_losing_last_city(bool native) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    s.native = native;
    capture_enemy_capital(s);
    EXPECT_TRUE(s.tribes[1].cities.empty());
    return s.tribes[1].result;
}

TEST(AgentAdvance, NoEliminationInAnObservation) {
    EXPECT_EQ(result_after_losing_last_city(true), poly::Result::Loss);
    EXPECT_EQ(result_after_losing_last_city(false), poly::Result::Incomplete);
}

// 04 §9 step 4: the next tribe is the first id after the active one that hasn't *lost*.
TEST(AgentAdvance, NextTribeSkipsOnlyLostTribes) {
    GameState s = level_with_capitals({{{2, 2}, 0}, {{2, 8}, 3}, {{8, 8}, 2}});
    ASSERT_EQ(s.num_tribes, 3);
    poly::start_turn(s, 0);
    s.tribes[1].result = poly::Result::Loss;
    advance(s, kEndTurn);
    EXPECT_EQ(s.active_tribe, 2);
    advance(s, kEndTurn);  // wraps to 0, tick unchanged
    EXPECT_EQ(s.active_tribe, 0);
    EXPECT_EQ(s.tick, 0);
}

}  // namespace
