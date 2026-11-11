// The tournament game loop (search/match.hpp; ADR-007, agents sheet 04 §4).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/rules.hpp"
#include "search/match.hpp"
#include "search/random_agent.hpp"

namespace {

using poly::Action;
using poly::GameState;

GameState two_tribes() {
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
    return poly::load_level(lines, poly::GameMode::Capitals, poly::ObsMode::Full, chance);
}

// Returns an action outside the legal set every `every`-th call that has a choice (with End turn
// alone legal, a no-op ends the turn as End turn would, without the Random draw), otherwise plays
// like Random.
class SometimesIllegal final : public poly::search::Agent {
   public:
    SometimesIllegal(std::uint64_t seed, int every) : random_(seed), every_(every) {}
    Action act(const poly::Observation& obs, std::span<const Action> legal) override {
        if (legal.size() > 1 && ++calls_ % every_ == 0) {
            return Action{.type = poly::ActionType::Disband, .actor = 999'999};
        }
        return random_.act(obs, legal);
    }

   private:
    poly::search::RandomAgent random_;
    int every_;
    int calls_ = 0;
};

// Agents sheet 04 §4: an action outside the legal set changes nothing and the agent is asked again.
// The wrapper consults its Random agent only on legal calls, so the game must be exactly the game
// of a plain Random agent with the same seed, plus the no-op decisions.
TEST(Match, IllegalActionIsANoOp) {
    const GameState s = two_tribes();
    SometimesIllegal a(5, 7);
    poly::search::RandomAgent b(6);
    const std::array<poly::search::Agent*, 2> with_illegal = {&a, &b};
    const auto r1 = poly::search::play_match(s, with_illegal, 42);
    poly::search::RandomAgent a2(5);
    poly::search::RandomAgent b2(6);
    const std::array<poly::search::Agent*, 2> plain = {&a2, &b2};
    const auto r2 = poly::search::play_match(s, plain, 42);
    EXPECT_GT(r1.illegal, 0);
    EXPECT_EQ(r2.illegal, 0);
    EXPECT_EQ(r1.decisions, r2.decisions + r1.illegal);
    EXPECT_EQ(r1.ticks, r2.ticks);
    EXPECT_EQ(r1.score, r2.score);
    EXPECT_EQ(r1.winner, r2.winner);
}

// ADR-007 §3: a game decided only by the final tie-break (equal on every ranking key, 04 §8.3) is a
// draw; one point of score difference is not.
TEST(Match, FullRankingTieIsADraw) {
    GameState s = two_tribes();
    s.tribes[0].score = s.tribes[1].score = 1000;
    s.tribes[0].techs = s.tribes[1].techs = 0;
    s.tick = 51;  // past the capitals turn limit (50)
    ASSERT_TRUE(poly::update_game_over(s));
    EXPECT_TRUE(s.ranking_tied);
    GameState t = two_tribes();
    t.tribes[0].score = 1000;
    t.tribes[1].score = 1001;
    t.tribes[0].techs = t.tribes[1].techs = 0;
    t.tick = 51;
    ASSERT_TRUE(poly::update_game_over(t));
    EXPECT_FALSE(t.ranking_tied);
    EXPECT_EQ(t.tribes[1].result, poly::Result::Win);
}

// Counts the legal non-End-turn actions it plays (the ones play_match must report as Action).
class Counting final : public poly::search::Agent {
   public:
    explicit Counting(std::uint64_t seed) : random_(seed) {}
    Action act(const poly::Observation& obs, std::span<const Action> legal) override {
        const Action a = random_.act(obs, legal);
        moves_ += a.type != poly::ActionType::EndTurn ? 1 : 0;
        return a;
    }
    [[nodiscard]] long long moves() const { return moves_; }

   private:
    poly::search::RandomAgent random_;
    long long moves_ = 0;
};

// A reported Move shows the state after it: the unit is on its destination.
bool arrived(const GameState& st, const Action& act) {
    if (act.type != poly::ActionType::Move) {
        return true;
    }
    const poly::Slot u = poly::unit_by_id(st, act.actor);
    return u != poly::kNoSlot && st.units[u].x == act.x && st.units[u].y == act.y;
}

// A reported turn start shows the state after start_turn: the tribe's units are refreshed.
bool refreshed(const GameState& st) {
    return std::ranges::none_of(st.units, [&](const poly::Unit& u) {
        return u.id != 0 && u.tribe == st.active_tribe && u.status != poly::TurnStatus::Fresh &&
               u.status != poly::TurnStatus::Moved;
    });
}

// Event counts and the number of out-of-order events (an Action outside its turn or by another
// tribe, a start inside a turn, an end outside one).
struct EventLog {
    int starts = 0;
    int ends = 0;
    long long actions = 0;
    long long moves = 0;
    int bad = 0;
    int turn_tribe = -1;
    bool in_turn = false;

    void on(const GameState& st, poly::search::MatchEvent e, const Action& act) {
        using poly::search::MatchEvent;
        if (e == MatchEvent::TurnStart) {
            bad += in_turn || !refreshed(st) ? 1 : 0;
            in_turn = true;
            turn_tribe = st.active_tribe;
            ++starts;
        } else if (e == MatchEvent::Action) {
            const bool ok =
                in_turn && st.active_tribe == turn_tribe && act.type != poly::ActionType::EndTurn;
            bad += ok && arrived(st, act) ? 0 : 1;
            ++actions;
            moves += act.type == poly::ActionType::Move ? 1 : 0;
        } else {
            bad += in_turn ? 0 : 1;
            in_turn = false;
            ++ends;
        }
    }
};

void expect_same_game(const poly::search::MatchResult& a, const poly::search::MatchResult& b) {
    EXPECT_EQ(a.decisions, b.decisions);
    EXPECT_EQ(a.ticks, b.ticks);
    EXPECT_EQ(a.score, b.score);
    EXPECT_EQ(a.winner, b.winner);
}

// The recording hook (tools/record.cpp) sees every executed action, between its turn's start and
// end, and cannot change the game.
TEST(Match, ObserverSeesEveryEventAndChangesNothing) {
    const GameState s = two_tribes();
    Counting a(5);
    Counting b(6);
    const std::array<poly::search::Agent*, 2> seats = {&a, &b};
    EventLog log;
    const auto r1 = poly::search::play_match(
        s, seats, 42, [&](const GameState& st, poly::search::MatchEvent e, const Action& act) {
            log.on(st, e, act);
        });
    EXPECT_EQ(log.bad, 0);
    EXPECT_FALSE(log.in_turn);
    EXPECT_EQ(log.starts, log.ends);
    EXPECT_GT(log.starts, 10);
    EXPECT_GT(log.moves, 10);  // the after-state check is exercised
    EXPECT_EQ(log.actions, a.moves() + b.moves());
    poly::search::RandomAgent a2(5);
    poly::search::RandomAgent b2(6);
    const std::array<poly::search::Agent*, 2> plain = {&a2, &b2};
    expect_same_game(r1, poly::search::play_match(s, plain, 42));
}

}  // namespace
