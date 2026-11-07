#pragma once

// Unit movement (rule facts 02 §1, §3; ADR-005 §4).

#include <array>

#include "poly/state.hpp"

namespace poly {

[[nodiscard]] inline bool is_naval(UnitType t) {
    return t == UnitType::Boat || t == UnitType::Ship || t == UnitType::Battleship;
}

// Whether the unit's turn status allows an attack, and the status after one (02 §1 table).
[[nodiscard]] bool can_attack(const Unit& u);
[[nodiscard]] TurnStatus status_after_attack(const Unit& u);

// Whether the unit's turn status allows a move (02 §1 table).
[[nodiscard]] bool can_move(const Unit& u);

// The status after a move (02 §1 table); only called when can_move holds.
[[nodiscard]] TurnStatus status_after_move(const Unit& u);

// Destinations of unit u, as tile indices in Tribes' generation order (the order tiles leave the
// priority queue), excluding occupied tiles. Replicates Tribes' search exactly (02 §3 pathfinding
// note): a java.util.PriorityQueue-like binary heap keyed on cost-so-far, costs lowered in place
// without re-heapifying, no re-expansion. Ignores the unit's turn status. Returns the count.
struct MoveTargets {
    std::array<std::uint16_t, kMaxTiles> tiles{};
    int count = 0;
};
void move_targets(const GameState& s, Slot u, MoveTargets& out);

}  // namespace poly
