// Root prioritization (agents sheet 01 §3) and MC (sheet 03 §2). Expected behaviour comes from the
// sheets: group availability and membership, End Turn redraws, the running-record "best", the
// ⌊tick/2⌋ opponent research, and the budget check (≥ 2,000, not >).

#include <gtest/gtest.h>

#include <bit>
#include <set>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/mc.hpp"
#include "search/root_groups.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::search::McAgent;

GameState
two_tribes() {  // 11 x 11 plains, capitals at (2, 2) and (8, 8), full obs, tribe 0 to move
    std::vector<std::string> lines(11, ".,.,.,.,.,.,.,.,.,.,.");
    lines[2] = ".,.,c:0,.,.,.,.,.,.,.,.";
    lines[8] = ".,.,.,.,.,.,.,.,c:2,.,.";
    poly::SampledChance chance(1);
    GameState s = poly::load_level(lines, poly::GameMode::Capitals, poly::ObsMode::Full, chance);
    poly::start_turn(s, 0);
    return s;
}

std::vector<Action> legal(const GameState& s) {
    poly::ActionList l;
    poly::legal_actions(s, l);
    return {l.begin(), l.end()};
}

Action of(ActionType t, int param = 0) {
    return Action{.type = t, .actor = 1, .param = static_cast<std::uint8_t>(param)};
}
const Action kEnd = of(ActionType::EndTurn);
const Action kResearch = of(ActionType::ResearchTech);
const Action kSpawn = of(ActionType::Spawn);
const Action kDestroy = of(ActionType::Destroy);
const Action kMove = of(ActionType::Move);
const Action kDisband = of(ActionType::Disband);

// Every candidate set root_candidates draws from `legal` over many seeds.
std::set<std::vector<int>> drawn_sets(const std::vector<Action>& legal) {
    std::set<std::vector<int>> out;
    std::vector<Action> c;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        poly::Rng rng(seed);
        poly::search::root_candidates(legal, rng, c);
        std::vector<int> types;
        types.reserve(c.size());
        for (const Action& a : c) {
            types.push_back(static_cast<int>(a.type));
        }
        out.insert(types);
    }
    return out;
}

std::vector<int> types(std::initializer_list<Action> l) {
    std::vector<int> t;
    for (const Action& a : l) {
        t.push_back(static_cast<int>(a.type));
    }
    return t;
}

TEST(RootGroups, EachAvailableGroupWithItsMembers) {
    const std::vector<Action> l = {kSpawn, kDestroy, kMove, kDisband, kEnd, kResearch};
    const std::set<std::vector<int>> expected = {types({kSpawn}), types({kMove, kDisband}),
                                                 types({kEnd, kResearch})};
    EXPECT_EQ(drawn_sets(l), expected);
}

TEST(RootGroups, DestroyAloneDoesNotMakeTheCityGroupAvailable) {
    EXPECT_EQ(drawn_sets({kDestroy, kMove}), std::set{types({kMove})});
}

TEST(RootGroups, EndTurnAloneDoesNotMakeTheTribeGroupAvailable) {
    EXPECT_EQ(drawn_sets({kEnd, kSpawn}), std::set{types({kSpawn})});
}

TEST(RootGroups, DisbandAloneMakesTheUnitGroupAvailable) {
    EXPECT_EQ(drawn_sets({kEnd, kDisband}), std::set{types({kDisband})});
}

TEST(RootGroups, NoGroupLeavesNoCandidates) {
    EXPECT_EQ(drawn_sets({kEnd, kDestroy}), std::set{std::vector<int>{}});
}

TEST(Mc, RunningBestKeepsTheRecordHolder) {  // 03 §2.1 quirk
    poly::search::RunningBest b(2);
    b.add(0, 10.0);  // A: mean 10, record 10
    b.add(1, 5.0);   // B: mean 5
    b.add(0, 0.0);   // A: mean 5, record stays 10
    b.add(1, 9.0);   // B: mean 7 > A's 5 but < the record
    EXPECT_EQ(b.best(), 0U);
    b.add(1, 26.0);  // B: mean 40/3 > 10
    EXPECT_EQ(b.best(), 1U);
}

TEST(Mc, RunningBestIsStrict) {
    poly::search::RunningBest b(2);
    b.add(0, 3.0);
    b.add(1, 3.0);  // equal mean: not strictly greater
    EXPECT_EQ(b.best(), 0U);
}

// The opponent research at `tick`, from the observation `ref` (tribe 0's).
void check_guess(const GameState& ref, int tick) {
    GameState s = ref;
    s.tick = tick;
    poly::SampledChance chance(3);
    poly::search::guess_opponent_techs(s, 0, chance);
    EXPECT_EQ(std::popcount(s.tribes[1].techs), tick / 2) << "tick " << tick;
    EXPECT_EQ(s.tribes[0].techs, ref.tribes[0].techs);  // P unchanged
    EXPECT_EQ(s.tribes[1].score, ref.tribes[1].score);  // no score
    EXPECT_EQ(s.tribes[1].stars, ref.tribes[1].stars);  // no cost
}

TEST(Mc, OpponentsGetHalfTheTickInRandomTechs) {  // 03 §2.2 step 1
    const GameState ref = poly::observe(two_tribes(), 0).view();
    ASSERT_EQ(ref.tribes[1].techs, 0U);  // hidden in the observation (04 §7)
    for (int tick : {0, 1, 5, 6}) {
        check_guess(ref, tick);
    }
}

TEST(Mc, SingleLegalActionIsReturnedWithoutSearch) {
    const auto obs = poly::observe(two_tribes(), 0);
    McAgent agent(1);
    EXPECT_EQ(agent.act(obs, std::vector{kDestroy}), kDestroy);
    EXPECT_EQ(agent.last_fm_calls(), 0);
}

TEST(Mc, NoGroupEndsTheTurnWithoutSearch) {
    const auto obs = poly::observe(two_tribes(), 0);
    McAgent agent(1);
    EXPECT_EQ(agent.act(obs, std::vector{kDestroy, kEnd}),
              (Action{.type = ActionType::EndTurn, .actor = 0}));
    EXPECT_EQ(agent.last_fm_calls(), 0);
}

TEST(Mc, NeverRollsOutEndTurnAtTheRoot) {  // redrawn (03 §2.1 a)
    // Tribe group = {End Turn, a research the copy can't execute}. The no-op never changes the
    // copy and keeps the stale list, so its every rollout is worth 0; a rollout from End Turn
    // would tie or differ and, being drawn first in about half the seeds, win the record.
    const auto obs = poly::observe(two_tribes(), 0);
    const Action noop{.type = ActionType::ResearchTech,
                      .actor = 0,
                      .param = static_cast<std::uint8_t>(poly::Tech::Farming)};
    const Action end{.type = ActionType::EndTurn, .actor = 0};
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        McAgent agent(seed);
        EXPECT_EQ(agent.act(obs, std::vector{end, noop}), noop) << "seed " << seed;
    }
}

TEST(Mc, UsesExactlyTheBudget) {  // checked ≥ 2,000 inside and after each rollout
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    const std::vector<Action> l = legal(s);
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        McAgent agent(seed);
        const Action a = agent.act(obs, l);
        EXPECT_NE(std::ranges::find(l, a), l.end());
        EXPECT_EQ(agent.last_fm_calls(), McAgent::kBudget);
    }
}

}  // namespace
