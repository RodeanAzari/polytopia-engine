// The tournament game loop (search/match.hpp; ADR-007, agents sheet 04 §4).

#include <gtest/gtest.h>

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

}  // namespace
