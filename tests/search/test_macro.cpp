// Macro mode (ADR-010). Expected behaviour comes from the ADR: per unit at most one move per goal
// (attack, siege, capture, explore, defend, Rule Based's pick), the destination closest to the
// goal's nearest target and only if it gets closer (ties: nearest the unit, then legal order);
// Destroy and Disband dropped; Send stars only at the largest amount per target; everything else
// kept; never reduced to End Turn alone; computed from the observation only.
// The Rule Based scores are passed in, so most tests use hand-written action lists and choose the
// "Rule Based's pick" themselves, away from the goal under test.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "search/macro.hpp"
#include "search/match.hpp"
#include "search/puct.hpp"
#include "search/rule_based.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::Slot;

// 11 x 11 plains, tribe 0's capital at (2, 2), tribe 1's at (8, 8), tribe 0 to move.
GameState two_tribes(poly::ObsMode mode = poly::ObsMode::Full) {
    std::vector<std::string> lines(11, ".,.,.,.,.,.,.,.,.,.,.");
    lines[2] = ".,.,c:0,.,.,.,.,.,.,.,.";
    lines[8] = ".,.,.,.,.,.,.,.,c:2,.,.";
    poly::SampledChance chance(1);
    GameState s = poly::load_level(lines, poly::GameMode::Capitals, mode, chance);
    poly::start_turn(s, 0);
    return s;
}

// A new unit of tribe t at (x, y) (test setup only); returns its actor id.
std::int32_t place(GameState& s, int t, int x, int y) {
    const Slot u = poly::new_unit(s);
    poly::Unit& unit = s.units[u];
    unit.id = s.next_actor_id++;
    unit.type = poly::UnitType::Warrior;
    unit.tribe = static_cast<std::int16_t>(t);
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    unit.hp = unit.max_hp = 10;
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
    s.tribes[static_cast<std::size_t>(t)].tribe_units.push_back(u);
    return unit.id;
}

Action move(std::int32_t unit, int x, int y) {
    return {.type = ActionType::Move,
            .actor = unit,
            .x = static_cast<std::uint8_t>(x),
            .y = static_cast<std::uint8_t>(y)};
}

// Kept actions (by value) for `legal` with the given Rule Based scores (default: all 0, so the
// Rule Based pick is legal[0]'s unit's first move).
std::vector<Action> kept(const GameState& s, const std::vector<Action>& legal,
                         std::vector<int> scores = {}) {
    if (scores.empty()) {
        scores.assign(legal.size(), 0);
    }
    poly::search::MacroFilter f;
    std::vector<std::size_t> keep;
    f.select(poly::observe(s, 0), legal, scores, keep);
    EXPECT_TRUE(std::ranges::is_sorted(keep));
    std::vector<Action> out;
    out.reserve(keep.size());
    for (const std::size_t i : keep) {
        out.push_back(legal.at(i));
    }
    return out;
}

bool has(const std::vector<Action>& v, const Action& a) {
    return std::ranges::find(v, a) != v.end();
}

TEST(Macro, PrunesDestroyAndDisbandKeepsOtherTypes) {  // ADR-010 §2, §3
    const GameState s = two_tribes();
    const Action end{.type = ActionType::EndTurn};
    const Action destroy{.type = ActionType::Destroy, .actor = 1, .x = 2, .y = 3};
    const Action disband{.type = ActionType::Disband, .actor = 5};
    const Action research{.type = ActionType::ResearchTech, .param = 3};
    const Action attack{.type = ActionType::Attack, .actor = 5, .target = 6};
    const Action road{.type = ActionType::BuildRoad, .x = 3, .y = 3};
    EXPECT_EQ(kept(s, {end, destroy, disband, research, attack, road}),
              (std::vector{end, research, attack, road}));
}

TEST(Macro, SendStarsKeepsTheLargestAmountPerTarget) {  // ADR-010 §2
    const GameState s = two_tribes();
    const auto send = [](int target, int amount) {
        return Action{.type = ActionType::SendStars,
                      .target = target,
                      .amount = static_cast<std::int16_t>(amount)};
    };
    const Action end{.type = ActionType::EndTurn};
    EXPECT_EQ(kept(s, {end, send(1, 1), send(1, 3), send(2, 2), send(1, 2), send(2, 1)}),
              (std::vector{end, send(1, 3), send(2, 2)}));
}

TEST(Macro, NeverReducesToEndTurnAlone) {  // ADR-010 §5
    const GameState s = two_tribes();
    const Action end{.type = ActionType::EndTurn};
    const Action destroy{.type = ActionType::Destroy, .actor = 1, .x = 2, .y = 3};
    const Action disband{.type = ActionType::Disband, .actor = 5};
    EXPECT_EQ(kept(s, {end, destroy, disband}), (std::vector{end, destroy, disband}));
    EXPECT_EQ(kept(s, {end}), (std::vector{end}));
}

TEST(Macro, AttackSiegeDefendAndRuleBasedEachKeepTheirOwnMove) {  // ADR-010 §1
    // Unit at (5, 1); enemy unit at (5, 4) (attack target, distance 3); enemy capital (8, 8)
    // (siege, 7); our capital (2, 2) is the own city nearest an enemy (defend, 3). Each kept
    // move is the unique best of exactly one goal, so dropping any goal changes the result.
    //            attack  siege  defend
    // (5, 0)       4       8      3     Rule Based's pick (first move, scores all 0)
    // (7, 4)       2       4      5     siege
    // (4, 3)       1       5      2     attack
    // (3, 1)       3       7      1     defend
    // (6, 0)       4       8      4     none
    GameState s = two_tribes();
    const auto u = place(s, 0, 5, 1);
    place(s, 1, 5, 4);
    place(s, 1, 10, 0);  // a farther enemy: the nearest one is the target
    const std::vector moves{move(u, 5, 0), move(u, 7, 4), move(u, 4, 3), move(u, 3, 1),
                            move(u, 6, 0)};
    EXPECT_EQ(kept(s, moves),
              (std::vector{move(u, 5, 0), move(u, 7, 4), move(u, 4, 3), move(u, 3, 1)}));
}

TEST(Macro, DefendTargetsTheOwnCityNearestAnEnemy) {  // ADR-010 §1
    GameState s = two_tribes();
    // A second own city at (2, 8), listed after the capital (2, 2); an enemy at (4, 9) is 2 from
    // it, while the capital's nearest enemy is the warrior on (8, 8), 6 away. So defend aims at
    // (2, 8), not at the first city in the list.
    const Slot c = poly::new_city(s);
    ASSERT_GT(c, s.tribes[0].capital);
    s.cities[c].id = s.next_actor_id++;
    s.cities[c].tribe = 0;
    s.cities[c].x = 2;
    s.cities[c].y = 8;
    place(s, 1, 4, 9);
    const auto u = place(s, 0, 5, 5);
    //            attack/siege (8, 8)   defend (2, 8)   (2, 2)
    // (6, 5)            3                   4             4     Rule Based's pick
    // (4, 6)            4                   2             4     defend
    // (6, 6)            2                   4             4     attack and siege
    const std::vector moves{move(u, 6, 5), move(u, 4, 6), move(u, 6, 6)};
    EXPECT_EQ(kept(s, moves), moves);
}

TEST(Macro, OnlyMovesThatGetCloserAreKept) {  // ADR-010 §1
    GameState s = two_tribes();
    const auto u = place(s, 0, 5, 5);  // enemy capital and warrior (8, 8) at distance 3
    const std::vector moves{move(u, 4, 4), move(u, 5, 6), move(u, 6, 6), move(u, 6, 4)};
    const auto k = kept(s, moves);
    EXPECT_TRUE(has(k, move(u, 6, 6)));  // distance 2; (5, 6) and (6, 4) stay at 3
    EXPECT_FALSE(has(k, move(u, 5, 6)));
    EXPECT_FALSE(has(k, move(u, 6, 4)));
}

TEST(Macro, CaptureGoalMovesTowardTheNearestVisibleVillage) {  // ADR-010 §1
    GameState s = two_tribes();
    s.terrain[static_cast<std::size_t>(s.index(0, 6))] = poly::Terrain::Village;
    const auto u = place(s, 0, 2, 6);
    const std::vector moves{move(u, 3, 7), move(u, 1, 6), move(u, 2, 7)};
    const auto k = kept(s, moves);
    EXPECT_TRUE(has(k, move(u, 1, 6)));  // village: distance 2 → 1
    EXPECT_FALSE(has(k, move(u, 2, 7)));
}

TEST(Macro, TiesGoToTheMoveNearestTheUnitThenLegalOrder) {  // ADR-010 §1
    GameState s = two_tribes();
    const auto u = place(s, 0, 5, 5);  // siege target (8, 8)
    // (7, 8), (8, 7) and (7, 7) are all at distance 1 from (8, 8); (7, 7) is 2 from the unit, the
    // others 3, so (7, 7) wins although it is listed last.
    const std::vector moves{move(u, 4, 4), move(u, 7, 8), move(u, 8, 7), move(u, 7, 7)};
    auto k = kept(s, moves);
    EXPECT_TRUE(has(k, move(u, 7, 7)));
    EXPECT_FALSE(has(k, move(u, 7, 8)));
    EXPECT_FALSE(has(k, move(u, 8, 7)));
    // Equal on both keys: the first in legal order.
    const std::vector tied{move(u, 4, 4), move(u, 7, 6), move(u, 6, 7)};
    k = kept(s, tied);
    EXPECT_TRUE(has(k, move(u, 7, 6)));
    EXPECT_FALSE(has(k, move(u, 6, 7)));
}

TEST(Macro, RuleBasedPickIsKept) {  // ADR-010 §1
    GameState s = two_tribes();
    const auto u = place(s, 0, 5, 5);
    // Targets: the enemy capital and its warrior on (8, 8) (siege, attack: distance 3) and our
    // capital (2, 2) (defend: distance 3). No move gets closer to either ((5, 4) only ties
    // defend's 3), so only the pick stays.
    const std::vector moves{move(u, 6, 4), move(u, 5, 4), move(u, 4, 5)};
    EXPECT_EQ(kept(s, moves, {0, 2, 3}), std::vector{move(u, 4, 5)});
}

TEST(Macro, EachUnitGetsItsOwnGoals) {
    GameState s = two_tribes();
    const auto a = place(s, 0, 5, 5);
    const auto b = place(s, 0, 1, 8);
    // Interleaved: b's moves must not be judged from a's position.
    const std::vector moves{move(a, 4, 4), move(b, 0, 8), move(a, 6, 6), move(b, 2, 8)};
    const auto k = kept(s, moves);
    EXPECT_TRUE(has(k, move(a, 4, 4)));  // a's Rule Based pick
    EXPECT_TRUE(has(k, move(a, 6, 6)));  // a's siege
    EXPECT_TRUE(has(k, move(b, 0, 8)));  // b's Rule Based pick
    EXPECT_TRUE(has(k, move(b, 2, 8)));  // b's siege: (8, 8) distance 7 → 6
}

TEST(Macro, ExploreGoalRevealsTheMostFogUnderPartialObs) {  // ADR-010 §1
    GameState s = two_tribes(poly::ObsMode::Partial);
    const auto u = place(s, 0, 2, 4);
    auto& vis = s.tribes[0].visible;
    vis.reset();
    for (int x = 0; x <= 4; ++x) {
        for (int y = 0; y <= 5; ++y) {
            vis.set(static_cast<std::size_t>(s.index(x, y)));
        }
    }
    // (2, 5) reveals row y = 6 (3 tiles), (3, 5) reveals 3 too but is listed later, (1, 3) none.
    // (4, 5) reveals (5, 4..6) and (3..5, 6): 5. Enemy city (8, 8) is fogged: no siege goal.
    const std::vector moves{move(u, 1, 3), move(u, 2, 5), move(u, 4, 5), move(u, 3, 5)};
    const auto k = kept(s, moves);
    EXPECT_TRUE(has(k, move(u, 1, 3)));  // Rule Based's pick
    EXPECT_TRUE(has(k, move(u, 4, 5)));  // explore
    EXPECT_FALSE(has(k, move(u, 2, 5)));
    EXPECT_FALSE(has(k, move(u, 3, 5)));
}

TEST(Macro, HiddenUnitsDoNotAttract) {  // no peeking through the fog
    GameState a = two_tribes(poly::ObsMode::Partial);
    const auto u = place(a, 0, 2, 4);
    auto& vis = a.tribes[0].visible;
    vis.reset();
    for (int x = 0; x <= 4; ++x) {
        for (int y = 0; y <= 7; ++y) {  // no fog next to any move: no explore goal
            vis.set(static_cast<std::size_t>(a.index(x, y)));
        }
    }
    GameState b = a;
    place(b, 1, 2, 8);  // in the fog: the observation is the same
    const std::vector moves{move(u, 1, 3), move(u, 1, 4), move(u, 2, 5)};
    EXPECT_EQ(kept(a, moves), kept(b, moves));
    // Visible, the same unit does attract (the test can tell).
    GameState c = a;
    place(c, 1, 2, 7);  // attack: (2, 5) gets closer
    EXPECT_NE(kept(a, moves), kept(c, moves));
}

TEST(Macro, MovesOfAUnitMissingFromTheObservationAreKept) {
    const GameState s = two_tribes();
    const std::vector moves{move(999, 4, 4), move(999, 5, 4)};
    EXPECT_EQ(kept(s, moves), moves);
}

// The macro subset of `legal` for the observer of `obs`, as the agent computes it: Rule Based
// scores, then the filter. `kept_scores` gets the kept actions' scores (their priors' input).
struct Reduced {
    std::vector<std::size_t> keep;
    std::vector<Action> actions;
    std::vector<int> kept_scores;
};
Reduced reduce(const poly::Observation& obs, std::span<const Action> legal) {
    Reduced r;
    std::vector<int> scores;
    long long cs = 0;
    poly::search::rule_based_scores(obs, legal, scores, cs);
    poly::search::MacroFilter f;
    f.select(obs, legal, scores, r.keep);
    r.actions.reserve(r.keep.size());
    r.kept_scores.reserve(r.keep.size());
    for (const std::size_t j : r.keep) {
        r.actions.push_back(legal[j]);
        r.kept_scores.push_back(scores[j]);
    }
    return r;
}

// Count of each unit's moves in `actions`.
std::map<std::int32_t, int> moves_per_unit(std::span<const Action> actions) {
    std::map<std::int32_t, int> out;
    for (const Action& a : actions) {
        out[a.actor] += a.type == ActionType::Move ? 1 : 0;
    }
    return out;
}

bool any_of_type(std::span<const Action> actions, ActionType t) {
    return std::ranges::any_of(actions, [t](const Action& a) { return a.type == t; });
}

// ADR-010 invariants on one real legal set: ascending legal indices; no Destroy/Disband unless
// it fell back to the full set; End Turn kept.
void expect_pruning(std::span<const Action> legal, const Reduced& r) {
    EXPECT_TRUE(std::ranges::is_sorted(r.keep));
    EXPECT_TRUE(r.keep.empty() || r.keep.back() < legal.size());
    const bool fell_back = r.keep.size() == legal.size();
    EXPECT_TRUE(fell_back || !any_of_type(r.actions, ActionType::Destroy));
    EXPECT_TRUE(fell_back || !any_of_type(r.actions, ActionType::Disband));
    EXPECT_EQ(any_of_type(legal, ActionType::EndTurn), any_of_type(r.actions, ActionType::EndTurn));
}

// Every unit with a move keeps 1..6 of them (Rule Based's pick plus at most five goals).
void expect_moves_kept(std::span<const Action> legal, const Reduced& r) {
    auto kept_moves = moves_per_unit(r.actions);
    for (const auto& [unit, n] : moves_per_unit(legal)) {
        const int k = kept_moves[unit];
        EXPECT_TRUE(n == 0 || (k >= 1 && k <= 6)) << "unit " << unit << ": " << k << " of " << n;
    }
}

// Plays Rule Based and checks the macro subset of every legal set it is given.
class Checker final : public poly::search::Agent {
   public:
    explicit Checker(std::uint64_t seed) : rb_(seed) {}
    Action act(const poly::Observation& obs, std::span<const Action> legal) override {
        const Reduced r = reduce(obs, legal);
        expect_pruning(legal, r);
        expect_moves_kept(legal, r);
        ++checked_;
        reduced_ += static_cast<long long>(legal.size() - r.keep.size());
        return rb_.act(obs, legal);
    }
    [[nodiscard]] long long checked() const { return checked_; }
    [[nodiscard]] long long reduced() const { return reduced_; }

   private:
    poly::search::RuleBasedAgent rb_;
    long long checked_ = 0;
    long long reduced_ = 0;
};

TEST(Macro, RealLegalSetsFromRuleBasedGames) {
    for (const auto mode : {poly::ObsMode::Full, poly::ObsMode::Partial}) {
        Checker a(1);
        Checker b(2);
        const std::array<poly::search::Agent*, 2> seats = {&a, &b};
        poly::search::play_match(two_tribes(mode), seats, 3);
        EXPECT_GT(a.checked(), 20);
        EXPECT_GT(a.reduced() + b.reduced(), 0);  // it does reduce something
    }
}

// Node n's edges and priors are the macro subset of its state's own legal set, judged from the
// mover's observation of that state (ADR-010 §4). Returns whether that subset is a reduction.
bool expect_node_reduced(const poly::search::PuctAgent& agent, std::size_t i) {
    const GameState& st = agent.state(i);
    poly::ActionList full;
    poly::legal_actions(st, full);
    const std::span<const Action> fl(full.begin(), full.end());
    const Reduced r = reduce(poly::observe(st, st.active_tribe), fl);
    const auto& n = agent.nodes()[i];
    const auto got = agent.actions(n);
    EXPECT_EQ(std::vector<Action>(got.begin(), got.end()), r.actions) << "node " << i;
    std::vector<double> priors;
    poly::search::softmax_priors(r.kept_scores, poly::search::PuctParams{}.temperature, priors);
    const auto edges = agent.edges(n);
    EXPECT_EQ(edges.size(), priors.size()) << "node " << i;
    for (std::size_t j = 0; j < std::min(edges.size(), priors.size()); ++j) {
        EXPECT_NEAR(edges[j].prior, priors[j], 1e-12) << "node " << i << " edge " << j;
    }
    return r.keep.size() < fl.size();
}

// Reduced non-terminal nodes below the root: all of them and those with the opponent to move.
std::pair<int, int> check_inner_nodes(const poly::search::PuctAgent& agent) {
    int reduced = 0;
    int opponent = 0;
    for (std::size_t i = 1; i < agent.nodes().size(); ++i) {
        if (!agent.nodes()[i].terminal && expect_node_reduced(agent, i)) {
            ++reduced;
            opponent += agent.state(i).active_tribe != 0 ? 1 : 0;
        }
    }
    return {reduced, opponent};
}

void expect_root(const poly::search::PuctAgent& agent, const Reduced& root) {
    const auto ra = agent.root_actions();
    EXPECT_EQ(std::vector<Action>(ra.begin(), ra.end()), root.actions);
    std::vector<double> priors;
    poly::search::softmax_priors(root.kept_scores, poly::search::PuctParams{}.temperature, priors);
    const auto re = agent.edges(agent.nodes()[0]);
    ASSERT_EQ(re.size(), priors.size());
    for (std::size_t i = 0; i < re.size(); ++i) {
        EXPECT_NEAR(re[i].prior, priors[i], 1e-12) << "edge " << i;
    }
}

// One macro-mode search with K determinizations from `obs` (ADR-010 §4, §5).
void check_macro_search(int k, const poly::Observation& obs, std::span<const Action> legal,
                        const Reduced& root) {
    SCOPED_TRACE(k);
    poly::search::PuctAgent agent(4, {.determinizations = k, .macro = true});
    EXPECT_TRUE(has(root.actions, agent.act(obs, legal)));
    EXPECT_EQ(agent.last_fm_calls(), poly::search::PuctAgent::kBudget);
    expect_root(agent, root);
    const auto [reduced, opponent] = check_inner_nodes(agent);
    EXPECT_GT(reduced, 100);  // the test can tell
    EXPECT_GT(opponent, 10);  // the mover's view, not the searcher's
}

TEST(Macro, PuctMacroModeSearchesTheMacroSubset) {
    GameState s = two_tribes();
    place(s, 0, 9, 4);  // off the diagonal: tribe 1's attack target differs from its other goals
    place(s, 0, 3, 7);
    poly::ActionList l;
    poly::legal_actions(s, l);
    const std::vector<Action> legal(l.begin(), l.end());
    const auto obs = poly::observe(s, 0);
    const Reduced root = reduce(obs, legal);
    ASSERT_LT(root.actions.size(), legal.size());  // the test can tell
    check_macro_search(0, obs, legal, root);
    check_macro_search(2, obs, legal, root);
}

}  // namespace
