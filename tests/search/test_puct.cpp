// PUCT (ADR-008). Expected behaviour comes from the ADR: the softmax prior, min/max normalisation,
// the PUCT score (opponent nodes score 1 − q̂), one forward-model call per expansion and 2,000 per
// decision, the first expansion following the largest prior, terminal values beyond every
// non-terminal one, the most-visited root action played.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/puct.hpp"
#include "search/rule_based.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::search::PuctAgent;

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

TEST(PuctFormula, SoftmaxPriors) {  // ADR-008 §3
    std::vector<double> p;
    const std::vector<int> scores{5, 4, -1};
    poly::search::softmax_priors(scores, 1.0, p);
    ASSERT_EQ(p.size(), 3U);
    const double z = 1.0 + std::exp(-1.0) + std::exp(-6.0);
    EXPECT_NEAR(p[0], 1.0 / z, 1e-12);
    EXPECT_NEAR(p[1], std::exp(-1.0) / z, 1e-12);
    EXPECT_NEAR(p[2], std::exp(-6.0) / z, 1e-12);
    poly::search::softmax_priors(scores, 2.0, p);  // a higher temperature flattens
    EXPECT_NEAR(p[1] / p[0], std::exp(-0.5), 1e-12);
    poly::search::softmax_priors(std::vector<int>{3, 3}, 1.0, p);
    EXPECT_DOUBLE_EQ(p[0], 0.5);
    poly::search::softmax_priors(std::vector<int>{}, 1.0, p);
    EXPECT_TRUE(p.empty());
}

TEST(PuctFormula, NormaliseAndScore) {  // ADR-008 §2
    using poly::search::normalise;
    using poly::search::puct_score;
    EXPECT_DOUBLE_EQ(normalise(3.0, 1.0, 5.0), 0.5);
    EXPECT_DOUBLE_EQ(normalise(1.0, 1.0, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(normalise(5.0, 1.0, 5.0), 1.0);
    EXPECT_DOUBLE_EQ(normalise(7.0, 2.0, 2.0), 0.5);  // no spread yet
    // q̂ + c·P·√N/(1+n)
    EXPECT_DOUBLE_EQ(puct_score(0.25, 0.5, 16, 3, 1.5, true), 0.25 + (1.5 * 0.5 * 4.0 / 4.0));
    // An opponent node scores the searching player's loss: 1 − q̂.
    EXPECT_DOUBLE_EQ(puct_score(0.25, 0.5, 16, 3, 1.5, false), 0.75 + (1.5 * 0.5 * 4.0 / 4.0));
    EXPECT_GT(puct_score(0.2, 0.1, 9, 0, 1.0, false), puct_score(0.8, 0.1, 9, 0, 1.0, false));
}

TEST(PuctFormula, SelectUsesTheNodesMeanForUnvisitedEdges) {  // ADR-008 §2
    using poly::search::PuctEdge;
    using poly::search::PuctNode;
    // Node mean 4 on [0, 10] → q̂ 0.4 for unvisited edges. Edge 0: visited, mean 3 (q̂ 0.3);
    // edge 1: unvisited. Equal priors, c tiny: the maximiser takes the unvisited edge (0.4 > 0.3),
    // the minimiser the visited one (1 − 0.3 > 1 − 0.4). With an unvisited mean of 0 instead,
    // both would flip.
    const PuctNode n{.visits = 5, .total = 20.0};
    const std::vector<PuctEdge> edges{{.prior = 0.5, .child = 1, .visits = 4, .total = 12.0},
                                      {.prior = 0.5}};
    EXPECT_EQ(poly::search::puct_select(n, edges, 0.0, 10.0, 1e-6, true), 1U);
    EXPECT_EQ(poly::search::puct_select(n, edges, 0.0, 10.0, 1e-6, false), 0U);
    // Priors decide among unvisited edges; the first wins exact ties.
    const std::vector<PuctEdge> fresh{{.prior = 0.2}, {.prior = 0.5}, {.prior = 0.3}};
    EXPECT_EQ(poly::search::puct_select(n, fresh, 0.0, 10.0, 1.0, true), 1U);
    const std::vector<PuctEdge> tie{{.prior = 0.5}, {.prior = 0.5}};
    EXPECT_EQ(poly::search::puct_select(n, tie, 0.0, 10.0, 1.0, false), 0U);
}

bool edges_match_children(const PuctAgent& agent, const poly::search::PuctNode& n) {
    return std::ranges::all_of(agent.edges(n), [&](const poly::search::PuctEdge& e) {
        return e.child >= 0 ? e.visits == agent.nodes()[static_cast<std::size_t>(e.child)].visits
                            : e.visits == 0;
    });
}

double sum_of(std::span<const poly::search::PuctEdge> edges,
              double poly::search::PuctEdge::* field) {
    return std::accumulate(
        edges.begin(), edges.end(), 0.0,
        [field](double s, const poly::search::PuctEdge& e) { return s + e.*field; });
}

// Tree bookkeeping every search must satisfy (ADR-008 §4/§5).
void check_tree(const PuctAgent& agent) {
    const auto nodes = agent.nodes();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const poly::search::PuctNode& n = nodes[i];
        EXPECT_TRUE(edges_match_children(agent, n)) << "node " << i;
        // Its own evaluation plus the backups through its edges (a finished game: revisits too).
        int below = 0;
        for (const poly::search::PuctEdge& e : agent.edges(n)) {
            below += e.visits;
        }
        EXPECT_TRUE(n.terminal || n.visits == 1 + below) << "node " << i;
        EXPECT_TRUE(n.num_edges == 0 ||
                    std::abs(sum_of(agent.edges(n), &poly::search::PuctEdge::prior) - 1.0) < 1e-9)
            << "node " << i;
    }
}

TEST(Puct, OneForwardModelCallPerExpansion) {
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        PuctAgent agent(seed);
        const auto l = legal(s);
        const Action a = agent.act(obs, l);
        EXPECT_NE(std::ranges::find(l, a), l.end());
        EXPECT_EQ(agent.last_fm_calls(), PuctAgent::kBudget);
        EXPECT_EQ(agent.nodes().size(), static_cast<std::size_t>(PuctAgent::kBudget) + 1);
        check_tree(agent);
    }
}

TEST(Puct, SingleLegalActionIsPlayedWithoutSearch) {
    const auto obs = poly::observe(two_tribes(), 0);
    PuctAgent agent(1);
    const Action end{.type = ActionType::EndTurn, .actor = 0};
    EXPECT_EQ(agent.act(obs, std::vector{end}), end);
    EXPECT_EQ(agent.last_fm_calls(), 0);
}

TEST(Puct, FirstExpansionFollowsTheLargestPrior) {  // ADR-008 §2/§3: all q̂ equal at first
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    const auto l = legal(s);
    long long convert = 0;
    std::vector<int> scores;
    scores.reserve(l.size());
    for (const Action& a : l) {
        scores.push_back(poly::search::rule_based_score(obs, a, convert));
    }
    const auto top = std::ranges::max_element(scores) - scores.begin();
    ASSERT_GT(top, 0);  // not the first action, or a prior-blind search would pass too
    PuctAgent agent(1);
    agent.act(obs, l);
    const auto root_edges = agent.edges(agent.nodes()[0]);
    const auto first = std::ranges::find_if(root_edges, [](const auto& e) { return e.child == 1; });
    ASSERT_NE(first, root_edges.end());
    EXPECT_EQ(first - root_edges.begin(), top);
}

TEST(Puct, PriorsComeFromThePlayerToMove) {  // ADR-008 §3
    // Observer 0, tribe 1 to move: the root's priors score tribe 1's actions from tribe 1's own
    // observation of the searched state (observer 0's copy), not from observer 0's.
    GameState s = two_tribes();
    poly::end_turn(s);
    poly::start_turn(s, 1);
    const auto obs0 = poly::observe(s, 0);
    const auto l = legal(obs0.view());
    const auto mover = poly::observe(obs0.view(), 1);
    long long convert = 0;
    std::vector<int> scores;
    std::vector<int> wrong;
    for (const Action& a : l) {
        scores.push_back(poly::search::rule_based_score(mover, a, convert));
        wrong.push_back(poly::search::rule_based_score(obs0, a, convert));
    }
    ASSERT_NE(scores, wrong);  // the test can tell the two apart
    std::vector<double> expected;
    poly::search::softmax_priors(scores, 1.0, expected);
    PuctAgent agent(1);
    agent.act(obs0, l);
    const auto root_edges = agent.edges(agent.nodes()[0]);
    ASSERT_EQ(root_edges.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_DOUBLE_EQ(root_edges[i].prior, expected[i]) << "edge " << i;
    }
}

TEST(Puct, PlaysTheMostVisitedRootAction) {  // ADR-008 §5
    const GameState s = two_tribes();
    const auto l = legal(s);
    PuctAgent agent(2);
    const Action a = agent.act(poly::observe(s, 0), l);
    const auto root_edges = agent.edges(agent.nodes()[0]);
    const auto most = std::ranges::max_element(
        root_edges, [](const auto& x, const auto& y) { return x.visits < y.visits; });
    EXPECT_EQ(a, l[static_cast<std::size_t>(most - root_edges.begin())]);
    EXPECT_GT(most->visits, 1);
}

// Mean value (searching player's view) of the most-visited root edge vs. the visit-weighted mean
// of the other visited root edges.
std::pair<double, double> most_visited_vs_rest(const PuctAgent& agent) {
    const auto root_edges = agent.edges(agent.nodes()[0]);
    const auto most = std::ranges::max_element(
        root_edges, [](const auto& x, const auto& y) { return x.visits < y.visits; });
    double total = 0.0;
    int visits = 0;
    for (const auto& e : root_edges) {
        if (&e != &*most) {
            total += e.total;
            visits += e.visits;
        }
    }
    return {most->total / most->visits, total / visits};
}

TEST(Puct, OpponentNodesMinimise) {  // ADR-008 §1/§2
    // Observer 0 while tribe 1 is to move: the root is an opponent node, so the search must
    // concentrate on what is worst for tribe 0. Control: tribe 1 searching the same position.
    // Flat priors and a small c make visits follow values (with the default, Rule Based priors
    // dominate the visit counts). Root actions: tribe 1's legal set in observer 0's copy (its
    // stars are hidden there), which tribe 1's own copy can also execute. No chance draws on this
    // level, so the search is the same for every seed.
    GameState s = two_tribes();
    poly::end_turn(s);
    poly::start_turn(s, 1);
    ASSERT_EQ(s.active_tribe, 1);
    const auto l = legal(poly::observe(s, 0).view());
    const poly::search::PuctParams value_driven{.c = 0.05, .temperature = 1000.0};
    PuctAgent opp(1, value_driven);
    opp.act(poly::observe(s, 0), l);
    const auto [most_o, rest_o] = most_visited_vs_rest(opp);
    EXPECT_LT(most_o, rest_o);
    PuctAgent own(1, value_driven);
    own.act(poly::observe(s, 1), l);
    const auto [most_m, rest_m] = most_visited_vs_rest(own);
    EXPECT_GT(most_m, rest_m);
}

// ADR-008 §4: a finished game is ±(the largest magnitude backed up before it + 1), signed by the
// result: beyond every value of the nodes created earlier (nodes are created, and first backed up,
// in index order). Returns the number of finished-game nodes.
int check_terminal_values(const PuctAgent& agent) {
    double largest = 0.0;
    int terminals = 0;
    for (std::size_t i = 0; i < agent.nodes().size(); ++i) {
        const auto& n = agent.nodes()[i];
        if (n.terminal) {
            ++terminals;
            const bool won = agent.state(i).tribes[0].result == poly::Result::Win;
            EXPECT_EQ(n.value > 0, won) << "node " << i;
            EXPECT_GE(std::abs(n.value), largest + 1.0) << "node " << i;
        }
        largest = std::max(largest, std::abs(n.value));
    }
    return terminals;
}

TEST(Puct, FinishedGamesRankBeyondEveryOtherValue) {  // ADR-008 §4
    // Past the turn limit, End Turn ends the game in the copy (04 §8.2).
    GameState s = two_tribes();
    s.tick = poly::kMaxTurnsCapitals + 1;
    const auto l = legal(s);
    PuctAgent agent(3);
    agent.act(poly::observe(s, 0), l);
    const int terminals = check_terminal_values(agent);
    ASSERT_GT(terminals, 0);
    // A found win dominates and revisits of it cost no forward-model call: the iteration cap
    // ends the search early instead of spinning (act()).
    EXPECT_LE(agent.last_fm_calls(), PuctAgent::kBudget);
    EXPECT_EQ(agent.nodes()[0].visits, 1 + PuctAgent::kMaxIterations);
    check_tree(agent);
}

TEST(Puct, SameSeedSameDecision) {  // CLAUDE.md §5
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    PuctAgent a(7);
    PuctAgent b(7);
    EXPECT_EQ(a.act(obs, legal(s)), b.act(obs, legal(s)));
}

}  // namespace
