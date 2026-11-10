// OSLA (agents sheet 03 §1). Expected behaviour comes from the sheet: skip set, End Turn as the
// default, strict best by the Diff heuristic, the 2,000-apply budget.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/diff_heuristic.hpp"
#include "search/osla.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::search::OslaAgent;

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

// Not legal for tribe 0 at the start (Farming needs Organization): a no-op in the forward model.
constexpr Action kNoOp{.type = ActionType::ResearchTech,
                       .actor = 0,
                       .param = static_cast<std::uint8_t>(poly::Tech::Farming)};
constexpr Action kEndTurn{.type = ActionType::EndTurn, .actor = 0};

// The Diff value of a, applied to a copy of obs (what OSLA evaluates).
double value_of(const poly::Observation& obs, const Action& a) {
    GameState next = obs.view();
    poly::SampledChance chance(7);
    poly::agent_advance(next, a, chance);
    return poly::search::diff_heuristic(obs.view(), next, obs.observer());
}

TEST(Osla, SingleLegalActionIsReturnedEvenIfSkippable) {
    const auto obs = poly::observe(two_tribes(), 0);
    const Action disband{.type = ActionType::Disband, .actor = 12345};
    OslaAgent agent(1);
    EXPECT_EQ(agent.act(obs, std::vector{disband}), disband);
}

TEST(Osla, EndTurnDestroyAndDisbandAreNeverEvaluated) {
    const auto obs = poly::observe(two_tribes(), 0);
    const std::vector<Action> l = {
        Action{.type = ActionType::Destroy, .actor = 1, .x = 2, .y = 2},
        Action{.type = ActionType::Disband, .actor = 12345},
        kEndTurn,
    };
    OslaAgent agent(1);
    EXPECT_EQ(agent.act(obs, l), kEndTurn);
}

TEST(Osla, AnyEvaluatedActionBeatsEndTurn) {
    const auto obs = poly::observe(two_tribes(), 0);
    ASSERT_EQ(value_of(obs, kNoOp), 0.0);
    OslaAgent agent(1);
    EXPECT_EQ(agent.act(obs, std::vector{kEndTurn, kNoOp}), kNoOp);
}

TEST(Osla, PlaysAnActionWithTheBestDiffValue) {
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    const std::vector<Action> l = legal(s);
    double best = -1e9;
    double worst = 1e9;
    for (const Action& a : l) {
        if (a.type != ActionType::EndTurn && a.type != ActionType::Destroy &&
            a.type != ActionType::Disband) {
            best = std::max(best, value_of(obs, a));
            worst = std::min(worst, value_of(obs, a));
        }
    }
    ASSERT_GT(best, worst);  // the choice is not trivial
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        OslaAgent agent(seed);
        EXPECT_EQ(value_of(obs, agent.act(obs, l)), best) << "seed " << seed;
    }
}

TEST(Osla, StopsAfter2000Applies) {
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    const std::vector<Action> all = legal(s);
    const auto good = std::ranges::find_if(all, [&](const Action& a) {
        return a.type != ActionType::EndTurn && value_of(obs, a) > 0.0;
    });
    ASSERT_NE(good, all.end());
    const Action positive = *good;
    std::vector<Action> l(OslaAgent::kBudget - 1, kNoOp);
    l.push_back(positive);  // the 2,000th evaluated action: still considered
    OslaAgent a(1);
    EXPECT_EQ(a.act(obs, l), positive);
    l.insert(l.begin(), kEndTurn);  // skipped: doesn't use budget
    OslaAgent b(1);
    EXPECT_EQ(b.act(obs, l), positive);
    l.insert(l.begin(), kNoOp);  // now the 2,001st: never applied
    OslaAgent c(1);
    EXPECT_EQ(c.act(obs, l), kNoOp);
}

}  // namespace
