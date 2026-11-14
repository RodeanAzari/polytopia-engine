// PUCT with a network (ADR-011 §2): the evaluator replaces the Rule Based priors and the Diff leaf
// value, and nothing else. A fake evaluator records every call; expectations come from the ADR: it
// is asked once per non-terminal node, for that node's edges, from the observation of the player to
// move; the priors land on the edges as returned; the value is the mover's, turned to the
// searching player's view (two players: negated at opponent nodes); finished games keep their exact
// result and are never sent to the network.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/diff_heuristic.hpp"
#include "search/leaf_eval.hpp"
#include "search/puct.hpp"

namespace {

using poly::Action;
using poly::GameState;
using poly::search::PuctAgent;

GameState two_tribes(poly::ObsMode mode) {
    std::vector<std::string> lines(11, ".,.,.,.,.,.,.,.,.,.,.");
    lines[2] = ".,.,c:0,.,.,.,.,.,.,.,.";
    lines[8] = ".,.,.,.,.,.,.,.,c:2,.,.";
    poly::SampledChance chance(1);
    GameState s = poly::load_level(lines, poly::GameMode::Capitals, mode, chance);
    poly::start_turn(s, 0);
    return s;
}

std::vector<Action> legal(const GameState& s) {
    poly::ActionList l;
    poly::legal_actions(s, l);
    return {l.begin(), l.end()};
}

struct Call {
    int observer = -1;
    int active = -1;
    bool game_over = false;
    std::vector<Action> actions;
};

// Priors proportional to 1, 2, 3, ... over the given actions (or all mass on `favourite`), and a
// value of `value` for whoever is to move.
class FakeNet final : public poly::search::LeafEvaluator {
   public:
    std::vector<Call> calls;
    double value = 0.25;
    Action favourite{};
    bool use_favourite = false;
    int searching = 0;  // begin_search minus end_search calls
    int searches = 0;   // begin_search calls
    int outside = 0;    // evaluate calls outside begin_search/end_search

    void begin_search() override {
        ++searching;
        ++searches;
    }
    void end_search() override { --searching; }

    void evaluate(const poly::Observation& obs, std::span<const Action> actions,
                  std::vector<double>& priors, double& v) override {
        outside += searching == 1 ? 0 : 1;
        calls.push_back({.observer = obs.observer(),
                         .active = obs.view().active_tribe,
                         .game_over = obs.view().game_over,
                         .actions = {actions.begin(), actions.end()}});
        priors = expected_priors(actions);
        v = value;
    }

    [[nodiscard]] std::vector<double> expected_priors(std::span<const Action> actions) const {
        std::vector<double> p(actions.size());
        double sum = 0;
        for (std::size_t i = 0; i < p.size(); ++i) {
            p[i] = static_cast<double>(i + 1);
            if (use_favourite) {
                p[i] = actions[i] == favourite ? 1000.0 : 1.0;
            }
            sum += p[i];
        }
        for (double& x : p) {
            x /= sum;
        }
        return p;
    }
};

// What is wrong with node i against the evaluator call it should have caused, or "" if nothing.
std::string node_problem(const PuctAgent& agent, const FakeNet& net, std::size_t i,
                         const Call& call) {
    const poly::search::PuctNode& n = agent.nodes()[i];
    const auto actions = agent.actions(n);
    if (call.observer != n.to_move || call.active != n.to_move || call.game_over) {
        return "not the mover's observation of a live game";
    }
    if (!std::ranges::equal(call.actions, actions)) {
        return "evaluator asked about other actions than the node's edges";
    }
    const auto want = net.expected_priors(actions);
    for (std::size_t k = 0; k < want.size(); ++k) {
        if (std::abs(agent.edges(n)[k].prior - want[k]) > 1e-12) {
            return "edge " + std::to_string(k) + " prior differs";
        }
    }
    const double v = n.to_move == 0 ? net.value : -net.value;  // tribe 0 searches
    return n.value == v ? "" : "value not in the searching player's view";
}

// `calls`: the evaluator calls this tree made, in order.
std::string tree_problem(const PuctAgent& agent, const FakeNet& net, std::span<const Call> calls) {
    std::size_t c = 0;
    for (std::size_t i = 0; i < agent.nodes().size(); ++i) {
        const poly::search::PuctNode& n = agent.nodes()[i];
        if (n.terminal) {
            if (std::abs(n.value) != 1.0 && n.value != 0.0) {
                return "finished game " + std::to_string(i) + " not valued exactly";
            }
            continue;
        }
        if (c >= calls.size()) {
            return "node " + std::to_string(i) + " was never evaluated";
        }
        if (auto p = node_problem(agent, net, i, calls[c++]); !p.empty()) {
            return "node " + std::to_string(i) + ": " + p;
        }
    }
    return c == calls.size() ? "" : "more evaluator calls than live nodes";
}

TEST(PuctNet, EveryLiveNodeIsEvaluatedFromTheMoversView) {
    const GameState s = two_tribes(poly::ObsMode::Full);
    FakeNet net;
    PuctAgent agent(3, {.determinizations = 0, .macro = false, .budget = 300}, &net);
    const auto l = legal(s);
    (void)agent.act(poly::observe(s, 0), l);
    EXPECT_EQ(agent.last_fm_calls(), 300);
    EXPECT_EQ(agent.nodes().size(), 301U);
    EXPECT_EQ(tree_problem(agent, net, net.calls), "");
    // The tree did reach the opponent's turn, so the sign flip was exercised.
    EXPECT_TRUE(std::ranges::any_of(agent.nodes(), [](const auto& n) { return n.to_move == 1; }));
}

TEST(PuctNet, MacroAndDeterminizedTreesUseTheNetworkToo) {
    const GameState s = two_tribes(poly::ObsMode::Partial);
    FakeNet net;
    PuctAgent agent(4, {.budget = 400}, &net);  // defaults: K = 4, macro
    const auto l = legal(s);
    (void)agent.act(poly::observe(s, 0), l);
    EXPECT_EQ(agent.last_fm_calls(), 400);
    // Only the last tree is kept: check its calls (each tree evaluates its own nodes).
    const std::size_t live = static_cast<std::size_t>(
        std::ranges::count_if(agent.nodes(), [](const auto& n) { return !n.terminal; }));
    ASSERT_GE(net.calls.size(), live);
    EXPECT_EQ(tree_problem(agent, net, std::span<const Call>(net.calls).last(live)), "");
}

TEST(PuctNet, EachSearchIsBracketedForTheBatcher) {
    // TorchEvaluator runs a batch once every thread inside begin/end_search waits: one bracket
    // per decision around all K trees, every evaluation inside it, and closed again afterwards.
    const GameState s = two_tribes(poly::ObsMode::Partial);
    FakeNet net;
    PuctAgent agent(5, {.budget = 200}, &net);
    const auto l = legal(s);
    (void)agent.act(poly::observe(s, 0), l);
    (void)agent.act(poly::observe(s, 0), l);
    EXPECT_FALSE(net.calls.empty());
    EXPECT_EQ(net.outside, 0);
    EXPECT_EQ(net.searches, 2);
    EXPECT_EQ(net.searching, 0);
}

TEST(PuctNet, FinishedGamesHoldTheirExactResult) {
    // Past the turn limit End Turn ends the game in the copy (04 §8.2): those nodes are valued by
    // the result in their own state (win +1, ranking tie 0, else -1), never by the network.
    GameState s = two_tribes(poly::ObsMode::Full);
    s.tick = poly::kMaxTurnsCapitals + 1;
    s.tribes[0].score = 5000;  // tribe 0 ranks first when the game ends
    FakeNet net;
    PuctAgent agent(3, {.determinizations = 0, .macro = false, .budget = 200}, &net);
    (void)agent.act(poly::observe(s, 0), legal(s));
    int terminals = 0;
    for (std::size_t i = 0; i < agent.nodes().size(); ++i) {
        if (!agent.nodes()[i].terminal) {
            continue;
        }
        const GameState& t = agent.state(i);
        double want = t.tribes[0].result == poly::Result::Win ? 1.0 : -1.0;
        if (t.ranking_tied) {
            want = 0.0;
        }
        EXPECT_EQ(agent.nodes()[i].value, want) << "node " << i;
        ++terminals;
    }
    EXPECT_GT(terminals, 0);
    EXPECT_EQ(tree_problem(agent, net, net.calls), "");
}

TEST(PuctNet, DiffLeafKeepsTheNetworksPriorsButNotItsValue) {
    // leaf=diff: priors from the network, values from the Diff heuristic vs. the root (ADR-008 §4)
    // in the searching player's own vision, computed here from each node's state.
    const GameState s = two_tribes(poly::ObsMode::Full);
    FakeNet net;
    PuctAgent agent(3, {.determinizations = 0, .macro = false, .budget = 200, .net_value = false},
                    &net);
    (void)agent.act(poly::observe(s, 0), legal(s));
    ASSERT_GT(net.calls.size(), 0U);
    std::size_t c = 0;
    for (std::size_t i = 0; i < agent.nodes().size(); ++i) {
        const poly::search::PuctNode& n = agent.nodes()[i];
        if (n.terminal) {
            continue;
        }
        const auto want = net.expected_priors(net.calls[c++].actions);
        for (std::size_t k = 0; k < want.size(); ++k) {
            ASSERT_NEAR(agent.edges(n)[k].prior, want[k], 1e-12) << "node " << i;
        }
        EXPECT_EQ(n.value, poly::search::diff_heuristic(agent.state(0), agent.state(i), 0,
                                                        poly::search::DiffVision::Player))
            << "node " << i;
    }
}

TEST(PuctNet, ThePriorSteersTheSearch) {
    // A flat value and one action with nearly all the prior mass: PUCT spends its visits there.
    const GameState s = two_tribes(poly::ObsMode::Full);
    const auto l = legal(s);
    for (const std::size_t pick : {std::size_t{0}, l.size() / 2, l.size() - 1}) {
        FakeNet net;
        net.value = 0.0;
        net.use_favourite = true;
        net.favourite = l[pick];
        PuctAgent agent(1, {.determinizations = 0, .macro = false, .budget = 200}, &net);
        EXPECT_EQ(agent.act(poly::observe(s, 0), l), l[pick]) << "pick " << pick;
    }
}

}  // namespace
