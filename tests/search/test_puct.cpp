// PUCT (ADR-008). Expected behaviour comes from the ADR: the softmax prior, min/max normalisation,
// the PUCT score (opponent nodes score 1 − q̂), one forward-model call per expansion and 2,000 per
// decision, the first expansion following the largest prior, terminal values beyond every
// non-terminal one, the most-visited root action played.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/dump.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/diff_heuristic.hpp"
#include "search/puct.hpp"
#include "search/rule_based.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::search::PuctAgent;

// ADR-008's flat single tree on the observation itself (K = 0, every legal action an edge; the
// defaults are K = 4 since SPRT 2 and macro mode since SPRT 3).
constexpr poly::search::PuctParams kOneTree{.determinizations = 0, .macro = false};

// 11 x 11 plains (no ruins: no chance draws), tribe 0 to move, full obs by default.
GameState two_tribes(poly::ObsMode mode = poly::ObsMode::Full) {
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

bool rejects(const char* spec) {
    try {
        (void)poly::search::parse_puct_params(spec);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

TEST(PuctParams, ParsesTheRunnerSpec) {  // A/B variants (ADR-007 §5)
    using poly::search::DiffVision;
    using poly::search::parse_puct_params;
    const auto d = parse_puct_params("");
    EXPECT_DOUBLE_EQ(d.c, 1.25);              // ADR-008 start value; c=2.5 rejected by SPRT 4
    EXPECT_DOUBLE_EQ(d.temperature, 0.25);    // ADR-008 §8: SPRT 6 (0.5), SPRT 7 (0.25)
    EXPECT_EQ(d.vision, DiffVision::Player);  // ADR-008 §8: adopted by SPRT 1
    const auto p = parse_puct_params("c=2.5:T=2:vision=own");
    EXPECT_DOUBLE_EQ(p.c, 2.5);
    EXPECT_DOUBLE_EQ(p.temperature, 2.0);
    EXPECT_EQ(p.vision, DiffVision::Player);
    EXPECT_EQ(parse_puct_params("vision=active").vision, DiffVision::ActiveTribe);
    EXPECT_EQ(d.determinizations, 4);  // ADR-008 §8: adopted by SPRT 2
    EXPECT_EQ(parse_puct_params("K=0").determinizations, 0);
    EXPECT_TRUE(d.macro);  // ADR-008 §8: adopted by SPRT 3
    EXPECT_TRUE(parse_puct_params("macro=1").macro);
    EXPECT_FALSE(parse_puct_params("macro=1:macro=0").macro);
    EXPECT_FALSE(parse_puct_params("macro=0").macro);
}

TEST(PuctParams, RejectsBadSpecs) {
    for (const char* bad :
         {"c", "c=", "c=x", "c=0", "c=-1", "T=1.5x", "T=inf", "vision=mine", "K=9", "K=-1", "K=2.5",
          "k=1", "c=1,T=2", "macro=2", "macro=", "macro=true"}) {
        EXPECT_TRUE(rejects(bad)) << bad;
    }
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
        PuctAgent agent(seed, kOneTree);
        const auto l = legal(s);
        const Action a = agent.act(obs, l);
        EXPECT_NE(std::ranges::find(l, a), l.end());
        EXPECT_EQ(agent.last_fm_calls(), PuctAgent::kBudget);
        EXPECT_EQ(agent.nodes().size(), static_cast<std::size_t>(PuctAgent::kBudget) + 1);
        check_tree(agent);
    }
}

TEST(Puct, MacroModeTreesKeepTheirBookkeeping) {  // ADR-010: same tree rules, fewer edges
    for (const auto mode : {poly::ObsMode::Full, poly::ObsMode::Partial}) {
        const GameState s = two_tribes(mode);
        PuctAgent agent(5, {.macro = true});
        const auto l = legal(s);
        const Action a = agent.act(poly::observe(s, 0), l);
        EXPECT_NE(std::ranges::find(l, a), l.end());
        EXPECT_EQ(agent.last_fm_calls(), PuctAgent::kBudget);
        check_tree(agent);
    }
}

TEST(Puct, SingleLegalActionIsPlayedWithoutSearch) {
    const auto obs = poly::observe(two_tribes(), 0);
    PuctAgent agent(1, kOneTree);
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
    PuctAgent agent(1, kOneTree);
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
    poly::search::softmax_priors(scores, kOneTree.temperature, expected);  // the agent's T
    PuctAgent agent(1, kOneTree);
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
    PuctAgent agent(2, kOneTree);
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
    PuctAgent agent(3, kOneTree);
    agent.act(poly::observe(s, 0), l);
    const int terminals = check_terminal_values(agent);
    ASSERT_GT(terminals, 0);
    // A found win dominates and revisits of it cost no forward-model call: the iteration cap
    // ends the search early instead of spinning (act()).
    EXPECT_LE(agent.last_fm_calls(), PuctAgent::kBudget);
    EXPECT_EQ(agent.nodes()[0].visits, 1 + PuctAgent::kMaxIterations);
    check_tree(agent);
}

TEST(Puct, LeafValueUsesTheConfiguredVision) {  // ADR-008 §4/§7, under fog where it matters
    using poly::search::DiffVision;
    // Flat priors so the tree reaches End Turn and the opponent's turn, where the two differ.
    const GameState s = two_tribes(poly::ObsMode::Partial);
    const auto obs = poly::observe(s, 0);
    const auto l = legal(obs.view());
    for (const DiffVision v : {DiffVision::ActiveTribe, DiffVision::Player}) {
        PuctAgent agent(
            1, {.temperature = 1000.0, .vision = v, .determinizations = 0, .macro = false});
        agent.act(obs, l);
        int differ = 0;  // nodes where the two visions disagree: the setting is exercised
        for (std::size_t i = 0; i < agent.nodes().size(); ++i) {
            if (agent.nodes()[i].terminal) {
                continue;
            }
            const GameState& st = agent.state(i);
            EXPECT_DOUBLE_EQ(agent.nodes()[i].value,
                             poly::search::diff_heuristic(obs.view(), st, 0, v));
            differ += poly::search::diff_heuristic(obs.view(), st, 0, DiffVision::ActiveTribe) !=
                              poly::search::diff_heuristic(obs.view(), st, 0, DiffVision::Player)
                          ? 1
                          : 0;
        }
        EXPECT_GT(differ, 0);
    }
}

TEST(Puct, DeterminizedTreesShareTheBudgetAndSumVisits) {  // ADR-009 §2
    GameState s = two_tribes();
    s.tick = 6;
    const auto obs = poly::observe(s, 0);
    const auto l = legal(s);
    PuctAgent agent(1, {.determinizations = 4, .macro = false});  // summed() in `l` order
    const Action a = agent.act(obs, l);
    EXPECT_EQ(agent.last_fm_calls(), PuctAgent::kBudget);  // 4 trees x 500
    int visits = 0;
    for (const auto& e : agent.summed()) {
        visits += e.visits;
    }
    EXPECT_EQ(visits, PuctAgent::kBudget);  // one root-edge visit per expansion, no finished games
    EXPECT_EQ(a, l[poly::search::most_visited(agent.summed())]);
    // The last tree searched a determinization: the opponent's hidden techs are filled in, which
    // the observation shows as none.
    ASSERT_EQ(obs.view().tribes[1].techs, 0U);
    EXPECT_NE(agent.state(0).tribes[1].techs, 0U);
    PuctAgent plain(1, kOneTree);
    plain.act(obs, l);
    EXPECT_EQ(plain.state(0).tribes[1].techs, 0U);
}

TEST(Puct, HiddenInformationDoesNotChangeTheDecision) {  // no peeking through the fog
    for (const int k : {0, 2}) {
        GameState a = two_tribes(poly::ObsMode::Partial);
        a.tick = 4;
        GameState b = a;
        b.tribes[1].stars += 20;
        poly::mark_researched(b.tribes[1], poly::Tech::Riding);
        b.cities[b.tribes[1].capital].population += 1;
        const auto oa = poly::observe(a, 0);
        const auto ob = poly::observe(b, 0);
        const auto l = legal(a);
        ASSERT_EQ(l, legal(b));
        PuctAgent pa(9, {.determinizations = k});
        PuctAgent pb(9, {.determinizations = k});
        EXPECT_EQ(pa.act(oa, l), pb.act(ob, l)) << "K " << k;
        EXPECT_EQ(poly::dump_state(pa.state(0)), poly::dump_state(pb.state(0))) << "K " << k;
    }
}

TEST(Puct, SameSeedSameDecision) {  // CLAUDE.md §5
    const GameState s = two_tribes();
    const auto obs = poly::observe(s, 0);
    PuctAgent a(7);
    PuctAgent b(7);
    EXPECT_EQ(a.act(obs, legal(s)), b.act(obs, legal(s)));
}

}  // namespace
