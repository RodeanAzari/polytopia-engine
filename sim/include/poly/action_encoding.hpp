#pragma once

// Action encoding and policy index (ADR-006). Every action of the active tribe maps to one index in
// [0, kPolicySize): a location tile in a fixed 24 x 24 frame times 332 channels, plus 130 global
// channels. Computed from the observation only; tile-based, so an index means the same action in
// every determinization of that observation. Encoding aborts loudly on anything outside the ADR's
// windows (never truncates).

#include <optional>

#include "poly/action.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"

namespace poly {

inline constexpr int kPolicyTiles = kMaxSide * kMaxSide;  // 576; tile t = 24 * x + y
inline constexpr int kPolicyChannels = 332;               // per tile (ADR-006 §1 table)
inline constexpr int kPolicyGlobal = 130;                 // End turn, Research, war, stars
inline constexpr int kPolicySize = (kPolicyTiles * kPolicyChannels) + kPolicyGlobal;  // 191,362

// Channel bases within a tile (ADR-006 §1), in table order.
namespace channel {
inline constexpr int kRecover = 0;
inline constexpr int kExamine = 1;
inline constexpr int kCapture = 2;
inline constexpr int kDisband = 3;
inline constexpr int kUpgrade = 4;
inline constexpr int kMakeVeteran = 5;
inline constexpr int kHealOthers = 6;
inline constexpr int kMove = 7;                   // 224 offsets, window radius 7
inline constexpr int kAttack = kMove + 224;       // 231: 48 offsets, radius 3
inline constexpr int kConvert = kAttack + 48;     // 279: 8 offsets, radius 1
inline constexpr int kLevelUp = kConvert + 8;     // 287: by LevelUpBonus
inline constexpr int kSpawn = kLevelUp + 8;       // 295: by UnitType
inline constexpr int kGather = kSpawn + 12;       // 307
inline constexpr int kClearForest = kGather + 1;  // 308
inline constexpr int kGrowForest = kGather + 2;   // 309
inline constexpr int kBurnForest = kGather + 3;   // 310
inline constexpr int kDestroy = kGather + 4;      // 311
inline constexpr int kBuildRoad = kGather + 5;    // 312
inline constexpr int kBuild = kGather + 6;        // 313: by Building, 19
static_assert(kBuild + 19 == kPolicyChannels);
inline constexpr int kMoveRadius = 7;     // 2M + 1 with M <= 3 (ADR-006)
inline constexpr int kAttackRadius = 3;   // max range
inline constexpr int kConvertRadius = 1;  // Mind Bender range
}  // namespace channel

// Global channels, after the spatial part (ADR-006 §1).
namespace global {
inline constexpr int kEndTurn = 0;
inline constexpr int kResearch = 1;     // + Tech
inline constexpr int kDeclareWar = 25;  // + relative tribe r = (Q - P - 1) mod n
inline constexpr int kSendStars = 32;   // + r * 14 + (amount - 1)
static_assert(kSendStars + (7 * 14) == kPolicyGlobal);
}  // namespace global

// The policy index of a legal action of the observer (the active tribe), from its observation.
[[nodiscard]] int encode_action(const Observation& obs, const Action& a);

// The legal action whose index is `index`, or nullopt if none has it. `legal` is the environment's
// legal list for the observer (04 §7).
[[nodiscard]] std::optional<Action> decode_action(const Observation& obs, const ActionList& legal,
                                                  int index);

namespace detail {
// Encodes against any state of the same shape: the observation's view, or (in tests only, to show
// the observation loses nothing) the true state. Agents use encode_action.
[[nodiscard]] int encode_in(const GameState& s, int tribe, const Action& a);
}  // namespace detail

}  // namespace poly
