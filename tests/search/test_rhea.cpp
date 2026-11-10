// RHEA (agents sheet 04). Expected behaviour comes from the sheet: init/shift by tick, the shift's
// stale tail (39 − j from a 20-action plan), ties accepting the mutant, the budget (≥ 2,000 after
// each mutation, every forward-model call counted), plans crossing End Turn into the opponent's
// turn.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/rhea.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::search::RheaAgent;

GameState two_tribes() {  // 11 x 11 plains (no ruins: no chance draws), tribe 0 to move, full obs
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

bool in(const std::vector<Action>& l, const Action& a) {
    return std::ranges::find(l, a) != l.end();
}

// 04 §3.2 step 2, from the sheet: how many of `plan`'s actions apply in order from `s` while each
// is feasible (game not over, in the copy's legal set).
int feasible_prefix(GameState s, std::span<const Action> plan) {
    poly::SampledChance chance(9);
    int j = 0;
    for (const Action& a : plan) {
        if (s.game_over || !in(legal(s), a)) {
            break;
        }
        poly::agent_advance(s, a, chance);
        ++j;
    }
    return j;
}

const Action kNoOp{.type = ActionType::ResearchTech,  // Farming needs Organization: never executes
                   .actor = 0,
                   .param = static_cast<std::uint8_t>(poly::Tech::Farming)};

TEST(Rhea, TiesAcceptTheMutantAndEveryCallCounts) {
    // Only a no-op is offered: every random action is it, nothing changes, every value is 0.
    const auto obs = poly::observe(two_tribes(), 0);
    RheaAgent agent(1);
    EXPECT_EQ(agent.act(obs, std::vector{kNoOp}), kNoOp);
    EXPECT_EQ(agent.plan().size(), 20U);
    EXPECT_EQ(agent.last_fm_calls(), 2000);  // init 20 + 99 mutations × 20
    EXPECT_EQ(agent.last_mutations(), 99);
    EXPECT_EQ(agent.last_accepted(), 99);  // 0 ≥ 0 every time
}

TEST(Rhea, BudgetIsCheckedAfterEachMutation) {
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        RheaAgent agent(seed);
        const Action a = agent.act(obs, legal(s));
        EXPECT_TRUE(in(legal(s), a));
        const int plan = static_cast<int>(agent.plan().size());
        EXPECT_GE(agent.last_fm_calls(), RheaAgent::kBudget);
        EXPECT_LT(agent.last_fm_calls() - plan,
                  RheaAgent::kBudget);  // the last mutation crossed it
        EXPECT_EQ(agent.last_fm_calls(), 20 + (agent.last_mutations() * plan));
    }
}

TEST(Rhea, ShiftKeepsTheStaleTailAndAppendsAfterIt) {  // 04 §3.2: 39 − j from length 20
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    int grew = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        RheaAgent agent(seed);
        agent.act(obs, legal(s));
        const std::vector<Action> before(agent.plan().begin(), agent.plan().end());
        ASSERT_EQ(before.size(), 20U);
        const int j = feasible_prefix(s, std::span(before).subspan(1));
        agent.act(obs, legal(s));  // same tick: the first action wasn't played, shift drops it
        const auto expected = static_cast<std::size_t>(19 + std::max(0, 20 - j));
        EXPECT_EQ(agent.plan().size(), expected) << "seed " << seed << ", j " << j;
        grew += j < 19 ? 1 : 0;
    }
    EXPECT_GT(grew, 0);  // the quirk was exercised
}

TEST(Rhea, NewTickStartsAFreshPlan) {
    const GameState s = two_tribes();
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        RheaAgent agent(seed);
        agent.act(poly::observe(s, 0), legal(s));
        GameState next = s;
        ++next.tick;
        agent.act(poly::observe(next, 0), legal(next));
        EXPECT_EQ(agent.plan().size(), 20U) << "seed " << seed;
    }
}

TEST(Rhea, PlanContinuesIntoTheOpponentsTurn) {  // 04 §1: after End Turn, the opponent's actions
    const GameState s = two_tribes();
    int crossed = 0;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        RheaAgent agent(seed);
        agent.act(poly::observe(s, 0), legal(s));
        const auto plan = agent.plan();
        GameState sim = poly::observe(s, 0).view();
        poly::SampledChance chance(9);
        for (std::size_t i = 0; i < plan.size() && !sim.game_over; ++i) {
            // A freshly initialised and mutated plan is feasible step by step from the root.
            ASSERT_TRUE(in(legal(sim), plan[i])) << "seed " << seed << ", step " << i;
            crossed += plan[i].type == ActionType::EndTurn && i + 1 < plan.size() ? 1 : 0;
            poly::agent_advance(sim, plan[i], chance);
        }
    }
    EXPECT_GT(crossed, 0);
}

}  // namespace

namespace {

TEST(Rhea, RandomActionsSkipDestroyAndDisband) {  // 04 §1
    const auto obs = poly::observe(two_tribes(), 0);
    const Action disband{.type = ActionType::Disband, .actor = 12345};
    const Action destroy{.type = ActionType::Destroy, .actor = 1, .x = 2, .y = 2};
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        RheaAgent agent(seed);
        agent.act(obs, std::vector{disband, kNoOp, destroy});
        EXPECT_TRUE(std::ranges::all_of(agent.plan(), [](const Action& a) { return a == kNoOp; }));
    }
}

TEST(Rhea, MutationExplores) {  // rate 0.1: some mutants differ and lose
    const GameState s = two_tribes();
    RheaAgent agent(1);
    agent.act(poly::observe(s, 0), legal(s));
    EXPECT_LT(agent.last_accepted(), agent.last_mutations());
    EXPECT_GT(agent.last_accepted(), 0);
    // ~10% of the mutated genes are rate draws: binomial(n, 0.1), mean 0.1 n, sd ≈ 0.3 √n; ±5 sd.
    const double n = agent.last_mutations() * static_cast<double>(agent.plan().size());
    EXPECT_NEAR(agent.last_rate_draws(), 0.1 * n, 5 * 0.3 * std::sqrt(n));
}

}  // namespace
