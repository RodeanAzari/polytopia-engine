#include "search/match.hpp"

#include <algorithm>
#include <utility>

#include "poly/chance.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"

namespace poly::search {

MatchResult play_match(GameState s, std::span<Agent* const> agents, std::uint64_t seed) {
    check(std::cmp_equal(agents.size(), s.num_tribes), "play_match: one agent per tribe");
    SampledChance chance(seed);
    ActionList legal;
    MatchResult r;
    while (!s.game_over) {
        const int before = s.tick;
        const int p = next_turn_tribe(s);
        if (s.tick != before && update_game_over(s)) {  // Tribes checks before each tick
            break;
        }
        start_turn(s, p);
        legal_actions(s, legal);
        bool ask = true;  // Tribes' continueTurn: decided before the chosen action runs
        while (ask) {
            const Action a = agents[static_cast<std::size_t>(p)]->act(observe(s, p),
                                                                      {legal.begin(), legal.end()});
            ++r.decisions;
            ask = legal.size() != 1 || legal[0].type != ActionType::EndTurn;
            if (a.type == ActionType::EndTurn) {
                break;  // Tribes ends the turn on End turn without checking it (04 §2)
            }
            if (std::ranges::find(legal, a) == legal.end()) {
                ++r.illegal;  // Tribes: the action can't execute, nothing changes
            } else {
                apply_action(s, a, chance);
                legal_actions(s, legal);
            }
            if (update_game_over(s)) {
                break;
            }
        }
        end_turn(s);
        update_game_over(s);
    }
    r.ticks = s.tick;
    r.draw = s.ranking_tied;
    for (int i = 0; i < s.num_tribes; ++i) {
        const TribeState& t = s.tribes[static_cast<std::size_t>(i)];
        r.score[static_cast<std::size_t>(i)] = t.score;
        if (t.result == Result::Win) {
            r.winner = i;
        }
    }
    return r;
}

}  // namespace poly::search
