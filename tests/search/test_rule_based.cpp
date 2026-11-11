// Rule Based scores (agents sheet 05). Each test pins one rule or quirk of Tribes' agent; expected
// values come from the sheet, not from our code.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"
#include "search/agent.hpp"
#include "search/match.hpp"
#include "search/rule_based.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::Slot;

GameState two_tribes() {  // 11 x 11 plains, Xin Xi at (2, 2), Bardur at (8, 8), full obs
    std::vector<std::vector<std::string>> grid(11, std::vector<std::string>(11, "."));
    grid[2][2] = "c:0";
    grid[8][8] = "c:2";
    std::vector<std::string> lines;
    for (const auto& row : grid) {
        std::string l;
        for (std::size_t i = 0; i < row.size(); ++i) {
            l += (i > 0 ? "," : "") + row[i];
        }
        lines.push_back(l);
    }
    poly::SampledChance chance(1);
    GameState s = poly::load_level(lines, poly::GameMode::Capitals, poly::ObsMode::Full, chance);
    poly::start_turn(s, 0);
    return s;
}

int score(const GameState& s, const Action& a) {
    long long converts = 0;
    return poly::search::rule_based_score(poly::observe(s, 0), a, converts);
}

Slot capital(const GameState& s, int t) { return s.tribes[static_cast<std::size_t>(t)].capital; }

// Places a new unit of tribe t at (x, y) (test setup only).
Slot place(GameState& s, int t, poly::UnitType type, int x, int y, int hp) {
    const Slot u = poly::new_unit(s);
    poly::Unit& unit = s.units[u];
    unit.type = type;
    unit.tribe = static_cast<std::int16_t>(t);
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    unit.hp = static_cast<std::int16_t>(hp);
    unit.max_hp = static_cast<std::int16_t>(poly::kUnitMaxHp[static_cast<std::size_t>(type)]);
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
    s.tribes[static_cast<std::size_t>(t)].tribe_units.push_back(u);
    return u;
}

void clear_tile(GameState& s, int x, int y) {
    const auto i = static_cast<std::size_t>(s.index(x, y));
    if (s.unit_at[i] != poly::kNoSlot) {
        s.units[s.unit_at[i]].x = 0;  // parked off the scene; only the tile matters here
        s.unit_at[i] = poly::kNoSlot;
    }
}

// Build: the intended tiers fall through to 1 for every listed building; Forest temple is 0.
// With needed = 5 (negative production), the score is the base itself.
TEST(RuleBased, BuildTiersFallThroughToOne) {
    GameState s = two_tribes();
    s.tribes[0].stars = 20;
    poly::City& c = s.cities[capital(s, 0)];
    c.population = -3;  // production -3, needed = 1 + 1 + 3 = 5
    const auto build = [&](poly::Building b) {
        return Action{.type = ActionType::Build,
                      .actor = c.id,
                      .x = 2,
                      .y = 3,
                      .param = static_cast<std::uint8_t>(b)};
    };
    EXPECT_EQ(score(s, build(poly::Building::Farm)), 1);        // intended 4
    EXPECT_EQ(score(s, build(poly::Building::Port)), 1);        // intended 3
    EXPECT_EQ(score(s, build(poly::Building::GrandBazar)), 1);  // intended 5
    EXPECT_EQ(score(s, build(poly::Building::ForestTemple)), 0);
    s.tribes[0].stars = 7;
    EXPECT_EQ(score(s, build(poly::Building::Farm)), 0);  // stars < 8
}

// Upgrade: the stricter ship branch never applies; production > 3 and stars > 6 give 3.
TEST(RuleBased, UpgradeUsesTheBoatRuleForShipsToo) {
    GameState s = two_tribes();
    s.cities[capital(s, 0)].level = 3;  // production 3 + 1 = 4
    const Slot ship = place(s, 0, poly::UnitType::Ship, 5, 5, 10);
    const Action up{.type = ActionType::Upgrade, .actor = s.units[ship].id};
    s.tribes[0].stars = 7;  // a ship rule of "stars > 8" would give 0
    EXPECT_EQ(score(s, up), 3);
    s.tribes[0].stars = 6;
    EXPECT_EQ(score(s, up), 0);
}

// Build road: an adjacent own city centre without the economy is no match and the scan goes on, so
// a neutral road later in the ring still scores 3.
TEST(RuleBased, RoadNextToCentreWithoutEconomyKeepsScanning) {
    GameState s = two_tribes();
    s.tribes[0].stars = 0;
    const Action road{.type = ActionType::BuildRoad, .actor = 0, .x = 3, .y = 3};
    EXPECT_EQ(score(s, road), 0);  // only the centre (2, 2) and own plain tiles around
    s.network.set(
        static_cast<std::size_t>(s.index(4, 4)));  // unowned road, after (2, 2) in the ring
    EXPECT_EQ(score(s, road), 3);
}

// Heal others: counts enemies in a ring of the *target's* range around the healer.
TEST(RuleBased, HealRingUsesTheTargetsRange) {
    GameState s = two_tribes();
    clear_tile(s, 5, 5);
    const Slot mb = place(s, 0, poly::UnitType::MindBender, 5, 5, 10);
    const Slot target = place(s, 0, poly::UnitType::Archer, 5, 6, 4);  // range 2, damaged
    place(s, 1, poly::UnitType::Warrior, 5, 7, 10);                    // 2 from the healer
    const Action heal{.type = ActionType::HealOthers, .actor = s.units[mb].id};
    EXPECT_EQ(score(s, heal), 1);
    s.units[target].type = poly::UnitType::Warrior;  // range 1: the enemy is out of the ring
    EXPECT_EQ(score(s, heal), 0);
}

// Move: stage 1 (approach a weaker visible enemy: 3) returns before stage 2 (a village: 5).
TEST(RuleBased, MoveStagesInOrder) {
    GameState s = two_tribes();
    const Slot w = place(s, 0, poly::UnitType::Warrior, 5, 5, 10);
    s.terrain[static_cast<std::size_t>(s.index(5, 6))] = poly::Terrain::Village;
    const Action move{.type = ActionType::Move, .actor = s.units[w].id, .x = 5, .y = 6};
    // A warrior (DEF 2, not below our ATK 2) that we approach is not "weaker": stage 1 doesn't
    // score.
    place(s, 1, poly::UnitType::Warrior, 5, 9, 10);
    EXPECT_EQ(score(s, move), 5);
    place(s, 1, poly::UnitType::Archer, 6, 9, 10);  // DEF 1 < ATK 2, same HP: approach scores 3
    EXPECT_EQ(score(s, move), 3);
}

// Move stage 1 considers only foreign units on the observer's visible tiles (sheet 05).
TEST(RuleBased, MoveStageOneSkipsUnseenUnits) {
    GameState s = two_tribes();
    const Slot w = place(s, 0, poly::UnitType::Warrior, 5, 5, 10);
    s.terrain[static_cast<std::size_t>(s.index(5, 6))] = poly::Terrain::Village;
    const Action move{.type = ActionType::Move, .actor = s.units[w].id, .x = 5, .y = 6};
    place(s, 1, poly::UnitType::Archer, 6, 9, 10);  // weaker and approached: 3 when seen
    EXPECT_EQ(score(s, move), 3);
    s.tribes[0].visible.reset(static_cast<std::size_t>(s.index(6, 9)));
    EXPECT_EQ(score(s, move), 5);  // unseen: stage 2's village decides
}

// Move stage 1 takes the first matching unit in x-major order (sheet 05): here the stronger
// archer at x = 4 (moved away from, in its range: 4) before the weaker one at x = 6 (approached:
// 3), although the latter comes first by y.
TEST(RuleBased, MoveStageOneScansXMajor) {
    GameState s = two_tribes();
    const Slot w = place(s, 0, poly::UnitType::Warrior, 5, 5, 10);
    const Action move{.type = ActionType::Move, .actor = s.units[w].id, .x = 5, .y = 4};
    place(s, 1, poly::UnitType::Archer, 4, 7, 15);  // HP above ours: not weaker; range 2
    place(s, 1, poly::UnitType::Archer, 6, 1, 5);   // DEF 1 < ATK 2, HP below ours: weaker
    EXPECT_EQ(score(s, move), 4);
}

// Fixed scores and End turn.
TEST(RuleBased, FixedScores) {
    const GameState s = two_tribes();
    EXPECT_EQ(score(s, Action{.type = ActionType::EndTurn}), -1);
    EXPECT_EQ(score(s, Action{.type = ActionType::DeclareWar, .target = 1}), 0);
    const auto research = [&](poly::Tech t) {
        return score(
            s, Action{.type = ActionType::ResearchTech, .param = static_cast<std::uint8_t>(t)});
    };
    EXPECT_EQ(research(poly::Tech::Organization), 5);
    EXPECT_EQ(research(poly::Tech::Chivalry), 4);
    EXPECT_EQ(research(poly::Tech::Philosophy), 3);
}

// The agent never picks an action scored below -1 and picks among the best uniformly.
TEST(RuleBased, PicksOnlyTheBestScore) {
    const GameState s = two_tribes();
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    const auto obs = poly::observe(s, 0);
    long long converts = 0;
    int best = -1;
    for (const Action& a : legal) {
        best = std::max(best, poly::search::rule_based_score(obs, a, converts));
    }
    poly::search::RuleBasedAgent agent(3);
    for (int k = 0; k < 50; ++k) {
        const Action a = agent.act(obs, {legal.begin(), legal.end()});
        EXPECT_EQ(poly::search::rule_based_score(obs, a, converts), best);
    }
}

}  // namespace

namespace {

// Plays uniformly at random and, at every decision, checks the batch scores against the
// one-action scores (rule_based_scores is an optimisation of rule_based_score, nothing else).
struct BatchStats {
    long long decisions = 0;
    long long mismatches = 0;
    long long with_foreign = 0;  // decisions where a foreign unit was visible (stage 1 can score)
};

bool sees_foreign(const poly::Observation& obs) {
    const poly::GameState& v = obs.view();
    const auto& visible = v.tribes[static_cast<std::size_t>(obs.observer())].visible;
    return std::ranges::any_of(v.units, [&](const poly::Unit& u) {
        return u.id != 0 && u.tribe != obs.observer() &&
               visible.test(static_cast<std::size_t>(v.index(u.x, u.y)));
    });
}

class BatchChecker final : public poly::search::Agent {
   public:
    BatchChecker(std::uint64_t seed, BatchStats& stats) : rng_(seed), stats_(&stats) {}
    Action act(const poly::Observation& obs, std::span<const Action> legal) override {
        long long converts = 0;
        poly::search::rule_based_scores(obs, legal, batch_, converts);
        ++stats_->decisions;
        bool same = batch_.size() == legal.size();
        for (std::size_t i = 0; same && i < legal.size(); ++i) {
            same = batch_[i] == poly::search::rule_based_score(obs, legal[i], converts);
        }
        stats_->mismatches += same ? 0 : 1;
        stats_->with_foreign += sees_foreign(obs) ? 1 : 0;
        return legal[rng_.below(static_cast<std::uint32_t>(legal.size()))];
    }

   private:
    poly::Rng rng_;
    BatchStats* stats_;
    std::vector<int> batch_;
};

BatchStats play_checked(poly::ObsMode mode, std::uint64_t seed) {
    GameState s = two_tribes();
    s.obs = mode;
    BatchStats stats;
    BatchChecker a(seed, stats);
    BatchChecker b(seed + 100, stats);
    std::array<poly::search::Agent*, 2> agents{&a, &b};
    poly::search::play_match(s, agents, seed);
    return stats;
}

TEST(RuleBased, BatchScoresEqualSingleScores) {
    using poly::ObsMode;
    const std::array<std::pair<ObsMode, std::uint64_t>, 6> games{{{ObsMode::Full, 1},
                                                                  {ObsMode::Full, 2},
                                                                  {ObsMode::Full, 3},
                                                                  {ObsMode::Partial, 1},
                                                                  {ObsMode::Partial, 2},
                                                                  {ObsMode::Partial, 3}}};
    for (const auto& [mode, seed] : games) {
        const BatchStats st = play_checked(mode, seed);
        EXPECT_EQ(st.mismatches, 0) << "seed " << seed;
        EXPECT_GT(st.with_foreign, 0) << "seed " << seed;  // stage 1 exercised
        EXPECT_GT(st.decisions, 200);
    }
}

}  // namespace
