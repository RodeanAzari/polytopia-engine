#pragma once

// One game between agents on our simulator, in Tribes' game loop (rule facts 04 §1–§2), the way
// tools/random_game.hpp plays its uniform-random games. Same state, seed and agents: same game.

#include <array>
#include <cstdint>
#include <functional>
#include <span>

#include "poly/state.hpp"
#include "search/agent.hpp"

namespace poly::search {

struct MatchResult {
    int winner = -1;    // tribe id ranked first
    bool draw = false;  // first and second tied on every ranking key (ADR-007 §3)
    int ticks = 0;      // the tick the game ended at
    long long decisions = 0;
    long long illegal = 0;  // actions returned outside the legal set (no-ops)
    std::array<int, kMaxTribes> score{};
};

enum class MatchEvent : std::uint8_t { TurnStart, Action, TurnEnd };

// Called with the true state after each turn start, executed action and turn end (as
// tools/random_game.hpp's play_random); `action` is meaningful for MatchEvent::Action only (End
// turn is reported as the TurnEnd that follows it; no-op illegal actions are not reported). For
// recording (tools/record.cpp); it cannot change the game.
using MatchObserver = std::function<void(const GameState&, MatchEvent, const Action&)>;

// Plays s to the end. agents[i] plays tribe id i. Random outcomes of the rules come from `seed`.
MatchResult play_match(GameState s, std::span<Agent* const> agents, std::uint64_t seed,
                       const MatchObserver& on_event = {});

}  // namespace poly::search
