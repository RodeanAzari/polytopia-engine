#pragma once

// A game action as the rules execute it. This is the simulator's internal form; the canonical
// action encoding for search and the policy head is a separate ADR (not decided yet). Actors are
// named by Tribes actor id (ADR-005), so logged actions map onto our state without translation.

#include <cstdint>

namespace poly {

// Only the types the simulator implements so far; more are added as the replay difftest reaches
// them (progress.md).
enum class ActionType : std::uint8_t {
    EndTurn,         // tribe
    ResearchTech,    // tribe, param = Tech
    DeclareWar,      // tribe, target = tribe
    SendStars,       // tribe, target = tribe, amount = stars
    BuildRoad,       // tribe, (x, y)
    GatherResource,  // city, (x, y), param = Resource
    LevelUp,         // city, param = LevelUpBonus
    Spawn,           // city, param = UnitType
    ClearForest,     // city, (x, y)
    GrowForest,      // city, (x, y)
    BurnForest,      // city, (x, y)
    Build,           // city, (x, y), param = Building
    Destroy,         // city, (x, y)
    Recover,         // unit
    Move,            // unit, (x, y) = destination
    Examine,         // unit
    Capture,         // unit; target = city actor id, or 0 for the village on its tile
    Attack,          // unit, target = target unit's actor id
    Disband,         // unit
    Upgrade,         // unit
    MakeVeteran,     // unit
};

struct Action {
    ActionType type = ActionType::EndTurn;
    std::int32_t actor = 0;   // tribe id for tribe actions, else the city's or unit's actor id
    std::int32_t target = 0;  // target tribe id, or target unit actor id
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t param = 0;  // enum value; meaning depends on type
    std::int16_t amount = 0;

    friend bool operator==(const Action&, const Action&) = default;
};

}  // namespace poly
