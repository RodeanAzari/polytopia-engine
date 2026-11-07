// Action generation (rule facts 04 §5, 03b). The replay difftest compares every legal set with
// the oracle's; these cover what its full-obs corpus never reaches (found by bug injection:
// visibility checks, a city founded inside another city's borders). Expected values come from the
// rule sheet, not from our code.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/rules.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::Slot;

constexpr int kXinXi = 0;
constexpr int kBardur = 2;

// An n x n plain level; `cells` overrides single cells, e.g. {{2, 2}, "c:0"} or {{3, 2}, "v"}.
std::vector<std::string> level(
    int n, const std::vector<std::pair<std::pair<int, int>, std::string>>& cells) {
    std::vector<std::vector<std::string>> grid(
        static_cast<std::size_t>(n), std::vector<std::string>(static_cast<std::size_t>(n), "."));
    for (const auto& [pos, cell] : cells) {
        grid[static_cast<std::size_t>(pos.first)][static_cast<std::size_t>(pos.second)] = cell;
    }
    std::vector<std::string> lines;
    for (const auto& row : grid) {
        std::string l;
        for (std::size_t i = 0; i < row.size(); ++i) {
            l += (i > 0 ? "," : "") + row[i];
        }
        lines.push_back(l);
    }
    return lines;
}

std::vector<Action> legal(const GameState& s) {
    poly::ActionList out;
    poly::legal_actions(s, out);
    return {out.begin(), out.end()};
}

std::vector<Action> of_type(const GameState& s, ActionType type) {
    std::vector<Action> out;
    for (const Action& a : legal(s)) {
        if (a.type == type) {
            out.push_back(a);
        }
    }
    return out;
}

Slot capital_unit(const GameState& s, int tribe) {
    return s.cities[s.tribes[static_cast<std::size_t>(tribe)].capital].units[0];
}

// Moves unit u to (x, y) directly (test setup only; no rules applied).
void teleport(GameState& s, Slot u, int x, int y) {
    poly::Unit& unit = s.units[u];
    s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = poly::kNoSlot;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
}

GameState two_tribes(const std::vector<std::pair<std::pair<int, int>, std::string>>& extra = {}) {
    auto cells = extra;
    cells.push_back({{2, 2}, "c:" + std::to_string(kXinXi)});
    cells.push_back({{8, 8}, "c:" + std::to_string(kBardur)});
    poly::SampledChance chance(1);  // full obs: never drawn
    return poly::load_level(level(11, cells), poly::GameMode::Capitals, poly::ObsMode::Full,
                            chance);
}

// 03b §8: Build road needs the tile visible to the tribe. Under full obs every tile is visible, so
// only a hand-made fog bit exercises the check.
TEST(LegalActions, RoadNeedsAVisibleTile) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    s.tribes[0].techs |= 1U << static_cast<unsigned>(poly::Tech::Roads);
    s.tribes[0].stars = 10;
    const auto road_at = [&](int x, int y) {
        return std::ranges::any_of(of_type(s, ActionType::BuildRoad), [&](const Action& a) {
            return std::cmp_equal(a.x, x) && std::cmp_equal(a.y, y);
        });
    };
    EXPECT_TRUE(road_at(5, 5));  // plain, unowned, no network, nobody on it
    s.tribes[0].visible.reset(static_cast<std::size_t>(s.index(5, 5)));
    EXPECT_FALSE(road_at(5, 5));
    EXPECT_TRUE(road_at(5, 6));
}

// 03b §3: a unit on an enemy city's centre may capture it, target = the city's actor id.
TEST(LegalActions, CaptureEnemyCity) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    const Slot u = capital_unit(s, 0);
    teleport(s, capital_unit(s, 1), 9, 8);  // Bardur's warrior out of its capital
    teleport(s, u, 8, 8);
    const auto captures = of_type(s, ActionType::Capture);
    ASSERT_EQ(captures.size(), 1U);
    EXPECT_EQ(captures[0].actor, s.units[u].id);
    EXPECT_EQ(captures[0].target, s.cities[s.tribes[1].capital].id);
}

// 03b §3 quirk: Capture looks up the tile's border owner and needs that city centred on the tile.
// A village inside Xin Xi's capital borders, captured by Xin Xi, keeps that owner (03b §3 step 3),
// so the new city can never be captured: no Capture for an enemy on it, and in particular no
// capture of the border-owning capital.
TEST(LegalActions, CityFoundedInsideBordersCannotBeCaptured) {
    GameState s = two_tribes({{{3, 2}, "v"}});
    const Slot capital0 = s.tribes[0].capital;
    ASSERT_EQ(s.city_owner[static_cast<std::size_t>(s.index(3, 2))], capital0);

    poly::start_turn(s, 0);
    const Slot w0 = capital_unit(s, 0);
    teleport(s, w0, 3, 2);
    const auto village = of_type(s, ActionType::Capture);
    ASSERT_EQ(village.size(), 1U);
    EXPECT_EQ(village[0].target, 0);  // the village on its tile
    poly::SampledChance chance(1);    // capital has a unit to give: no shuffle (03b §3 step 5)
    poly::apply_action(s, village[0], chance);
    ASSERT_EQ(s.tribes[0].cities.size(), 2U);
    EXPECT_EQ(s.city_owner[static_cast<std::size_t>(s.index(3, 2))], capital0);

    poly::start_turn(s, 1);
    teleport(s, w0, 5, 5);
    teleport(s, capital_unit(s, 1), 3, 2);
    EXPECT_TRUE(of_type(s, ActionType::Capture).empty());
}

// 04 §5: a city that can level up makes its two variants the only actions; no End turn.
TEST(LegalActions, LevelUpReplacesEverything) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    s.cities[s.tribes[0].capital].population = 2;  // needs 2 at level 1
    const auto all = legal(s);
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[0].type, ActionType::LevelUp);
    EXPECT_EQ(all[0].param, static_cast<int>(poly::LevelUpBonus::Workshop));
    EXPECT_EQ(all[1].param, static_cast<int>(poly::LevelUpBonus::Explorer));
}

// 03b §7: Convert targets another tribe's unit in range whose tile is visible. Full obs never hides
// a tile, so only a hand-made fog bit exercises the check.
TEST(LegalActions, ConvertNeedsAVisibleTarget) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    const Slot mb = capital_unit(s, 0);
    s.units[mb].type = poly::UnitType::MindBender;
    teleport(s, mb, 5, 5);
    const Slot enemy = capital_unit(s, 1);
    teleport(s, enemy, 5, 6);
    const auto converts = of_type(s, ActionType::Convert);
    ASSERT_EQ(converts.size(), 1U);
    EXPECT_EQ(converts[0].actor, s.units[mb].id);
    EXPECT_EQ(converts[0].target, s.units[enemy].id);
    s.tribes[0].visible.reset(static_cast<std::size_t>(s.index(5, 6)));
    EXPECT_TRUE(of_type(s, ActionType::Convert).empty());
}

// 03b §7 / 02 §1: Heal others needs a Mind Bender that can attack (Fresh only). A pushed Mind
// Bender starts its turn Moved: it may not heal, though it is not Finished.
TEST(LegalActions, HealOthersNeedsAFreshMindBender) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    const Slot mb = capital_unit(s, 0);
    s.units[mb].type = poly::UnitType::MindBender;
    teleport(s, mb, 5, 5);
    poly::SampledChance chance(1);
    poly::apply_action(s,
                       {.type = ActionType::Spawn,
                        .actor = s.cities[s.tribes[0].capital].id,
                        .param = static_cast<std::uint8_t>(poly::UnitType::Warrior)},
                       chance);
    const Slot ally = s.unit_at[static_cast<std::size_t>(s.index(2, 2))];
    ASSERT_NE(ally, poly::kNoSlot);
    teleport(s, ally, 5, 6);
    s.units[ally].hp = 3;  // below max
    EXPECT_EQ(of_type(s, ActionType::HealOthers).size(), 1U);
    s.units[mb].status = poly::TurnStatus::Moved;
    EXPECT_TRUE(of_type(s, ActionType::HealOthers).empty());
}

TEST(LegalActions, NoneOnceTheGameIsOver) {
    GameState s = two_tribes();
    poly::start_turn(s, 0);
    EXPECT_FALSE(legal(s).empty());
    s.game_over = true;
    EXPECT_TRUE(legal(s).empty());
}

}  // namespace
