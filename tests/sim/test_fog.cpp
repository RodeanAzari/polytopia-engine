// Partial observability (rule facts 03b §1, 03a §5-6, ADR-004). The partial-obs replay difftest
// checks these against Tribes in whole games; these cover what its corpus doesn't reach (found by
// bug injection): road reveals, clear-view order, the explorer's network update, Eye of God, and a
// meeting draw that grants nothing. Expected values come from the rule sheet, not from our code.

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/network.hpp"
#include "poly/rules.hpp"

namespace {

using poly::GameState;
using poly::Slot;

// Records meeting draws (always index 0) and plays scripted explorer steps (then 0). Anything
// else fails the test.
class ScriptedChance final : public poly::Chance {
   public:
    std::vector<int> meeting_counts;
    std::deque<int> explorer_steps;

    poly::RuinBonus ruin_bonus(bool /*exclude_research*/) override {
        ADD_FAILURE() << "unexpected ruin draw";
        return poly::RuinBonus::Resources;
    }
    int random_tech(int /*count*/) override {
        ADD_FAILURE() << "unexpected random tech draw";
        return 0;
    }
    int meeting_tech(int count) override {
        meeting_counts.push_back(count);
        return 0;
    }
    int explorer_neighbour(int /*count*/) override {
        if (explorer_steps.empty()) {
            return 0;
        }
        const int v = explorer_steps.front();
        explorer_steps.pop_front();
        return v;
    }
    void shuffle_cities(std::span<std::uint16_t> /*cities*/) override {}
};

// An n x n level, plain unless overridden: {{x, y}, "cell"}.
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

GameState load(const std::vector<std::pair<std::pair<int, int>, std::string>>& cells,
               poly::ObsMode obs = poly::ObsMode::Partial) {
    ScriptedChance chance;  // the cities below are too far apart to meet at load
    return poly::load_level(level(11, cells), poly::GameMode::Capitals, obs, chance);
}

std::size_t tile(const GameState& s, int x, int y) {
    return static_cast<std::size_t>(s.index(x, y));
}

void teleport(GameState& s, Slot u, int x, int y) {
    poly::Unit& unit = s.units[u];
    s.unit_at[tile(s, unit.x, unit.y)] = poly::kNoSlot;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    s.unit_at[tile(s, x, y)] = u;
}

Slot capital_unit(const GameState& s, int tribe) {
    return s.cities[s.tribes[static_cast<std::size_t>(tribe)].capital].units[0];
}

void grant(poly::TribeState& t, poly::Tech tech) { t.techs |= 1U << static_cast<unsigned>(tech); }

// 03b §1: revealing a road (network flag, not water, not city) or a water tile requests a network
// update; revealing other land, or an already visible road, does not.
TEST(Fog, RevealOfRoadOrWaterRequestsNetworkUpdate) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}, {{5, 8}, "s"}});
    ScriptedChance chance;
    EXPECT_FALSE(poly::clear_view(s, 0, 5, 5, 0, chance));  // hidden plain, no road
    s.network.set(tile(s, 5, 6));
    EXPECT_TRUE(poly::clear_view(s, 0, 5, 6, 0, chance));   // hidden road
    EXPECT_FALSE(poly::clear_view(s, 0, 5, 6, 0, chance));  // already visible
    EXPECT_TRUE(poly::clear_view(s, 0, 5, 8, 0, chance));   // hidden water
    EXPECT_EQ(s.tribes[0].visible.count(),
              16U + 3U);  // load view r2 clipped at the edge (4x4), + 3
}

// 03b §1: the neighbourhood is viewed before the centre, so a neighbour's tribe is met first.
TEST(Fog, ClearViewMeetsNeighboursBeforeTheCentre) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 1}, "c:2"}, {{1, 9}, "c:3"}});
    teleport(s, capital_unit(s, 1), 5, 5);  // centre
    teleport(s, capital_unit(s, 2), 4, 4);  // first neighbour in x-major order
    ScriptedChance chance;
    (void)poly::clear_view(s, 0, 5, 5, 1, chance);
    const auto& met = s.tribes[0].met;
    ASSERT_EQ(met.size(), 3U);
    EXPECT_EQ(met[1], 2);
    EXPECT_EQ(met[2], 1);
}

// 03b §1 quirk: the meeting tech is drawn among techs the other tribe has and P lacks, but granted
// only if researchable. Smithery without Mining: drawn (one candidate), not granted.
TEST(Fog, MeetingTechIsDrawnButGrantedOnlyIfResearchable) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    s.tribes[0].techs = 0;
    s.tribes[1].techs = 0;
    grant(s.tribes[1], poly::Tech::Smithery);
    teleport(s, capital_unit(s, 1), 5, 5);
    ScriptedChance chance;
    (void)poly::clear_view(s, 0, 5, 5, 0, chance);
    EXPECT_EQ(chance.meeting_counts, (std::vector<int>{1}));
    EXPECT_FALSE(s.tribes[0].has_tech(poly::Tech::Smithery));

    GameState r = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});  // Climbing (a root): granted
    r.tribes[0].techs = 0;
    r.tribes[1].techs = 0;
    grant(r.tribes[1], poly::Tech::Climbing);
    teleport(r, capital_unit(r, 1), 5, 5);
    (void)poly::clear_view(r, 0, 5, 5, 0, chance);
    EXPECT_TRUE(r.tribes[0].has_tech(poly::Tech::Climbing));
    EXPECT_EQ(r.tribes[0].score, s.tribes[0].score);  // no score for a meeting tech
}

// 03a §5: Eye of God becomes available when the last tile is revealed, under partial obs only.
TEST(Fog, EyeOfGodWhenEveryTileIsVisible) {
    const auto eye = [](GameState& g) {
        return poly::monument(g.tribes[0], poly::Building::EyeOfGod);
    };
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    ScriptedChance chance;
    for (int x = 0; x < 11; ++x) {  // every tile but (10, 10)
        for (int y = 0; y < 11; ++y) {
            if (x != 10 || y != 10) {
                (void)poly::clear_view(s, 0, x, y, 0, chance);
            }
        }
    }
    EXPECT_EQ(s.tribes[0].visible.count(), 120U);
    EXPECT_EQ(eye(s), poly::MonumentStatus::Unavailable);
    (void)poly::clear_view(s, 0, 10, 10, 0, chance);
    EXPECT_EQ(eye(s), poly::MonumentStatus::Available);

    GameState f = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}}, poly::ObsMode::Full);
    (void)poly::clear_view(f, 0, 5, 5, 5, chance);
    EXPECT_EQ(eye(f), poly::MonumentStatus::Unavailable);
}

// 03b §6 / Tribes' Board.launchExplorer: an explorer revealing water recomputes the tribe's
// network. Ports at (2,2) (capital) and (2,5) (second city) are 3 water steps apart, but (2,4) is
// hidden, so they are not linked until the explorer reveals it.
TEST(Fog, ExplorerRevealUpdatesTheNetwork) {
    GameState s = load({{{1, 1}, "c:0"},
                        {{9, 9}, "c:2"},
                        {{1, 6}, "v"},
                        {{2, 2}, "s"},
                        {{2, 3}, "s"},
                        {{2, 4}, "s"},
                        {{2, 5}, "s"}});
    ScriptedChance chance;
    poly::start_turn(s, 0);
    const Slot w = capital_unit(s, 0);
    teleport(s, w, 1, 6);
    poly::apply_action(s, {.type = poly::ActionType::Capture, .actor = s.units[w].id, .target = 0},
                       chance);
    ASSERT_EQ(s.tribes[0].cities.size(), 2U);
    const Slot second = s.tribes[0].cities[1];
    for (const auto& [x, y] : {std::pair{2, 2}, std::pair{2, 5}}) {
        s.building[tile(s, x, y)] = poly::Building::Port;
        s.network.set(tile(s, x, y));
    }
    poly::recompute_network_tribe(s, 0);
    ASSERT_FALSE(s.tribes[0].visible.test(tile(s, 2, 4)));
    ASSERT_FALSE(s.tribes[0].connected.contains(second));

    grant(s.tribes[0], poly::Tech::Sailing);  // the explorer may enter shallow water
    s.cities[s.tribes[0].capital].population = 2;
    chance.explorer_steps = {7, 4};  // (1,1) -> (2,2) -> (2,3), which reveals (2,4)
    poly::apply_action(s,
                       {.type = poly::ActionType::LevelUp,
                        .actor = s.cities[s.tribes[0].capital].id,
                        .param = static_cast<std::uint8_t>(poly::LevelUpBonus::Explorer)},
                       chance);
    EXPECT_TRUE(s.tribes[0].visible.test(tile(s, 2, 4)));
    EXPECT_TRUE(s.tribes[0].connected.contains(second));
}

}  // namespace
