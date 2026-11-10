// MCTS (agents sheet 02). Expected behaviour comes from the sheet: UCT with min/max normalisation
// (§3), opponent nodes minimising (§3), the budget arithmetic (§4), the final choice and its
// equal-visits fallback (§5), and the slots at depth 0 vs. ≥ 1 (§2).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/mcts.hpp"
#include "search/root_groups.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::search::ChildStats;
using poly::search::MctsAgent;

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

std::vector<Action> actions_of(const MctsAgent& agent, const poly::search::MctsNode& n) {
    std::vector<Action> out;
    for (const auto& sl : agent.slots(n)) {
        out.push_back(sl.action);
    }
    return out;
}

TEST(MctsUct, MeanUnnormalisedUntilTwoDistinctValues) {
    const ChildStats c{.total = 6.0, .visits = 3};  // mean 2
    const double explore = std::numbers::sqrt2 * std::sqrt(std::log(10.0 + 1.0) / 3.0);
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_NEAR(poly::search::uct_value(c, 10, inf, -inf), 2.0 + explore, 1e-5);  // unset
    EXPECT_NEAR(poly::search::uct_value(c, 10, 4.0, 4.0), 2.0 + explore, 1e-5);   // all equal
    EXPECT_NEAR(poly::search::uct_value(c, 10, 0.0, 8.0), 0.25 + explore, 1e-5);  // (2-0)/(8-0)
}

TEST(MctsUct, UnvisitedChildIsHugelyAttractive) {  // visits + ε in the denominators
    const ChildStats c{.total = 0.0, .visits = 0};
    EXPECT_GT(poly::search::uct_value(c, 5, 0.0, 1.0), 1e3);
}

TEST(MctsUct, OpponentNodesMinimise) {
    const std::vector<ChildStats> children = {{.total = 9.0, .visits = 3},   // mean 3
                                              {.total = 3.0, .visits = 3}};  // mean 1
    poly::Rng rng(1);
    EXPECT_EQ(poly::search::uct_select(children, 6, 0.0, 4.0, true, rng), 0U);
    EXPECT_EQ(poly::search::uct_select(children, 6, 0.0, 4.0, false, rng), 1U);
}

TEST(MctsFinal, MostVisits) {
    const std::vector<ChildStats> c = {{.total = 100.0, .visits = 2}, {.total = 1.0, .visits = 5}};
    poly::Rng rng(1);
    EXPECT_EQ(poly::search::final_choice(c, rng), 1U);
}

TEST(MctsFinal, EqualVisitsFallBackToTheBestMean) {
    const std::vector<ChildStats> c = {
        {.total = 1.0, .visits = 4}, {.total = 7.0, .visits = 4}, {.total = 3.0, .visits = 4}};
    poly::Rng rng(1);
    EXPECT_EQ(poly::search::final_choice(c, rng), 1U);
}

TEST(Mcts, SingleLegalActionAndNoGroupDoNotSearch) {
    const auto obs = poly::observe(two_tribes(), 0);
    const Action destroy{.type = ActionType::Destroy, .actor = 1};
    const Action end{.type = ActionType::EndTurn, .actor = 0};
    MctsAgent agent(1);
    EXPECT_EQ(agent.act(obs, std::vector{destroy}), destroy);
    EXPECT_EQ(agent.last_fm_calls(), 0);
    EXPECT_EQ(agent.act(obs, std::vector{destroy, end}), end);
    EXPECT_EQ(agent.last_fm_calls(), 0);
}

// One searched decision from the start position.
struct Searched {
    GameState s = two_tribes();
    std::vector<Action> l = legal(s);
    MctsAgent agent{3};
    Action chosen;
    Searched() : chosen(agent.act(poly::observe(s, 0), l)) {}
};

TEST(Mcts, BudgetCountsExpansionsAndSelectionSteps) {  // 02 §4
    const Searched r;
    const auto nodes = r.agent.nodes();
    ASSERT_GT(nodes.size(), 1U);
    // No leaf reached depth 20 here (checked), so every iteration expanded one node at depth d,
    // costing d (d - 1 descents + 1 forward-model call).
    long long cost = 0;
    int deepest = 0;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        cost += nodes[i].depth;
        deepest = std::max(deepest, nodes[i].depth);
    }
    ASSERT_LT(deepest, MctsAgent::kDepth);
    EXPECT_EQ(r.agent.last_fm_calls(), cost);
    EXPECT_GT(cost, MctsAgent::kBudget);                             // stops only once it exceeds
    EXPECT_LE(cost - nodes.back().depth, MctsAgent::kBudget);        // checked after each iteration
    EXPECT_EQ(nodes[0].visits, static_cast<int>(nodes.size()) - 1);  // one backup per iteration
}

TEST(Mcts, RootSlotsAreTheCandidatesDeeperSlotsAllLegalActions) {  // 02 §2
    const Searched r;
    const auto nodes = r.agent.nodes();
    const std::vector<Action> root_slots = actions_of(r.agent, nodes[0]);
    bool matches_a_group = false;  // the root is one prioritized group (seed-independent check)
    for (std::uint64_t seed = 0; seed < 64 && !matches_a_group; ++seed) {
        poly::Rng rng(seed);
        std::vector<Action> c;
        poly::search::root_candidates(r.l, rng, c);
        matches_a_group = c == root_slots;
    }
    EXPECT_TRUE(matches_a_group);
    int checked = 0;
    for (std::size_t i = 1; i < nodes.size() && checked < 50; ++i) {
        EXPECT_EQ(actions_of(r.agent, nodes[i]), legal(r.agent.state(i))) << "node " << i;
        ++checked;
    }
}

// Node i's visits, total and bounds against its children (02 §2 backup).
void check_backup(const MctsAgent& agent, std::size_t i) {
    const auto nodes = agent.nodes();
    const auto& n = nodes[i];
    int child_visits = 0;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    for (const auto& sl : agent.slots(n)) {
        if (sl.child >= 0) {
            const auto& c = nodes[static_cast<std::size_t>(sl.child)];
            child_visits += c.visits;
            lo = std::min(lo, c.lo);
            hi = std::max(hi, c.hi);
        }
    }
    EXPECT_GE(lo, n.lo);  // a node saw every value its children saw
    EXPECT_LE(hi, n.hi);
    // A node's own evaluation (on expansion) plus every visit passed down to a child.
    EXPECT_EQ(n.visits, child_visits + (i == 0 ? 0 : 1)) << "node " << i;
    const double m = n.total / n.visits;
    EXPECT_LE(n.lo, m + 1e-9);
    EXPECT_GE(n.hi, m - 1e-9);
}

TEST(Mcts, BackupKeepsVisitsTotalsAndBounds) {
    const Searched r;
    for (std::size_t i = 0; i < r.agent.nodes().size(); ++i) {
        check_backup(r.agent, i);
    }
}

TEST(Mcts, ChoosesTheMostVisitedRootChild) {  // 02 §5
    const Searched r;
    const auto nodes = r.agent.nodes();
    int most = -1;
    int chosen_visits = -1;
    for (const auto& sl : r.agent.slots(nodes[0])) {
        ASSERT_GE(sl.child, 0);  // 2,000 units fully expand the root here
        const int v = nodes[static_cast<std::size_t>(sl.child)].visits;
        most = std::max(most, v);
        if (sl.action == r.chosen) {
            chosen_visits = v;
        }
    }
    EXPECT_EQ(chosen_visits, most);
}

}  // namespace

namespace {

TEST(Mcts, NoOpChildInheritsTheStaleList) {  // rules 04 §9: Tribes keeps the copy's cached list
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    // Not executable in the copy (Farming needs Organization): a no-op root action. With End Turn
    // it forms the Tribe group, the only one available.
    const Action noop{.type = ActionType::ResearchTech,
                      .actor = 0,
                      .param = static_cast<std::uint8_t>(poly::Tech::Farming)};
    const Action end{.type = ActionType::EndTurn, .actor = 0};
    // Destroy alone doesn't make the City group available: candidates {noop, end} ≠ root_legal.
    const Action destroy{.type = ActionType::Destroy, .actor = 1, .x = 2, .y = 2};
    const std::vector<Action> root_legal = {noop, end, destroy};
    MctsAgent agent(5);
    agent.act(obs, root_legal);
    const auto nodes = agent.nodes();
    bool seen = false;
    for (const auto& sl : agent.slots(nodes[0])) {
        if (sl.action == noop && sl.child >= 0) {
            EXPECT_EQ(actions_of(agent, nodes[static_cast<std::size_t>(sl.child)]),
                      root_legal);  // the environment's list, not recomputed
            seen = true;
        }
    }
    EXPECT_TRUE(seen);
}

}  // namespace

namespace {

TEST(Mcts, BudgetBoundaryOverSeeds) {  // 02 §4: stop once > 2,000; some seed lands on 2,000
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    const std::vector<Action> l = legal(s);
    for (std::uint64_t seed = 1; seed <= 16; ++seed) {
        MctsAgent agent(seed);
        agent.act(obs, l);
        const auto nodes = agent.nodes();
        EXPECT_GT(agent.last_fm_calls(), MctsAgent::kBudget) << "seed " << seed;
        EXPECT_LE(agent.last_fm_calls() - nodes.back().depth, MctsAgent::kBudget) << seed;
    }
}

TEST(Mcts, ExpansionPicksARandomEmptySlot) {  // 02 §2: not the first one
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    const std::vector<Action> l = legal(s);
    bool other_than_first = false;
    for (std::uint64_t seed = 1; seed <= 8 && !other_than_first; ++seed) {
        MctsAgent agent(seed);
        agent.act(obs, l);
        other_than_first = agent.slots(agent.nodes()[0])[0].child != 1;
    }
    EXPECT_TRUE(other_than_first);
}

// Visit-weighted minus plain mean of node n's children's means.
double visit_bias(const MctsAgent& agent, const poly::search::MctsNode& n) {
    double weighted = 0.0;
    double plain = 0.0;
    int visits = 0;
    int count = 0;
    for (const auto& sl : agent.slots(n)) {
        const auto& c = agent.nodes()[static_cast<std::size_t>(sl.child)];
        weighted += c.total;
        plain += c.total / c.visits;
        visits += c.visits;
        ++count;
    }
    return (weighted / visits) - (plain / count);
}

TEST(Mcts, OpponentNodesSteerTowardsLowValues) {  // 02 §3, at the call site
    // Tribe group {End Turn, a no-op research}: the End Turn child is an opponent node that gets
    // about half the budget; a minimiser visits its low-valued children more.
    const auto obs = poly::observe(two_tribes(), 0);
    const Action noop{.type = ActionType::ResearchTech,
                      .actor = 0,
                      .param = static_cast<std::uint8_t>(poly::Tech::Farming)};
    const Action end{.type = ActionType::EndTurn, .actor = 0};
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        MctsAgent agent(seed);
        agent.act(obs, std::vector{end, noop});
        const auto root = agent.slots(agent.nodes()[0]);
        const auto it =
            std::ranges::find_if(root, [&](const auto& sl) { return sl.action == end; });
        ASSERT_NE(it, root.end());
        const auto child = static_cast<std::size_t>(it->child);
        const auto& opp = agent.nodes()[child];
        ASSERT_EQ(agent.state(child).active_tribe, 1);
        ASSERT_EQ(opp.expanded, opp.num_slots);
        EXPECT_LT(visit_bias(agent, opp), 0.0) << "seed " << seed;
    }
}

}  // namespace
