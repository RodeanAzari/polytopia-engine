#pragma once

// Network input features (ADR-011 §1), computed from an Observation only: the type makes "no
// peeking" a compile-time property (ADR-005 §2). Compact form: per tile, one class index per
// categorical field, a flag byte and a few scalars; plus a global vector. The network expands the
// classes into one-hot planes itself (ADR-011 §1 amendment), so C++ inference and Python training
// share one expansion and records stay small.
//
// Perspective: everything is relative to the observer P. Tribe Q is r = (Q - P) mod n, r = 0 is P.
// Tiles are row-major over the map's own side (x * side + y), not the 24 x 24 policy frame;
// network_index() maps an ADR-006 policy index onto the network's output at that side.

#include <array>
#include <cstdint>

#include "poly/action_encoding.hpp"
#include "poly/observation.hpp"

namespace poly {

inline constexpr int kFeaturesVersion = 1;  // bump on any change: networks and records carry it

// Per-tile categorical fields. Field f has kTileClasses[f] classes; the last class is "none".
namespace field {
inline constexpr int kTerrain = 0;     // Terrain (Fog included), 8 classes, never none
inline constexpr int kResource = 1;    // Resource as observed (masked by tech), 7 + none
inline constexpr int kBuilding = 2;    // Building, 19 + none
inline constexpr int kTerritory = 3;   // owner r 0..7, 8 = a tribe the observer can't name, 9 none
inline constexpr int kCityOwner = 4;   // a city on this tile: its tribe r 0..7, 8 = none
inline constexpr int kUnitType = 5;    // UnitType, 12 + none
inline constexpr int kUnitOwner = 6;   // r 0..7, 8 = none
inline constexpr int kUnitStatus = 7;  // TurnStatus, 6 + none
inline constexpr int kCarried = 8;     // naval units: the land unit inside, 12 + none
inline constexpr int kCount = 9;
}  // namespace field
inline constexpr std::array<int, field::kCount> kTileClasses = {8, 8, 20, 10, 9, 13, 9, 7, 13};

// Per-tile flag bits.
namespace flag {
inline constexpr std::uint8_t kVisible = 1U << 0U;  // visible to the observer
inline constexpr std::uint8_t kNetwork = 1U << 1U;  // trade network
inline constexpr std::uint8_t kCapital = 1U << 2U;  // city on this tile is a capital
inline constexpr std::uint8_t kWalls = 1U << 3U;    // city on this tile has walls
inline constexpr std::uint8_t kVeteran = 1U << 4U;  // unit on this tile is a veteran
inline constexpr int kCount = 5;
}  // namespace flag

// Per-tile scalars (0 where the thing is absent). Scales are fixed, not learned.
namespace scalar {
inline constexpr int kCityLevel = 0;       // level / 10
inline constexpr int kCityPopulation = 1;  // population / need, own cities only (others' hidden)
inline constexpr int kCityBorder = 2;      // border radius / 3
inline constexpr int kUnitHp = 3;          // hp / max_hp (0 if max_hp is 0)
inline constexpr int kCount = 4;
}  // namespace scalar

// Global vector layout (ADR-011 §1).
namespace gfeat {
inline constexpr int kStars = 0;         // P's stars / 20
inline constexpr int kTechs = 1;         // + Tech: P's tech bits (24)
inline constexpr int kTribeType = 25;    // + TribeType: P's tribe one-hot (12)
inline constexpr int kScore = 37;        // + r: score / 1000 of tribe r = 0..7 (0 if r >= n)
inline constexpr int kTick = 45;         // tick / 50
inline constexpr int kPartialObs = 46;   // 1 under partial observability
inline constexpr int kScoreMode = 47;    // 1 in Score mode
inline constexpr int kCanEndTurn = 48;   // P's can_end_turn bit
inline constexpr int kTurnMustEnd = 49;  // turn_must_end
inline constexpr int kLevelingUp = 50;   // leveling_up
inline constexpr int kNumTribes = 51;    // n / 8
inline constexpr int kMonuments = 52;    // + 4 * monument + MonumentStatus: P's (7 x 4 one-hot)
inline constexpr int kSize = kMonuments + (kNumMonuments * 4);  // 80
}  // namespace gfeat

struct Features {
    int side = 0;
    // [tile][field], tile = x * side + y over side * side tiles
    std::array<std::uint8_t, static_cast<std::size_t>(kMaxTiles) * field::kCount> fields{};
    std::array<std::uint8_t, kMaxTiles> flags{};
    std::array<float, static_cast<std::size_t>(kMaxTiles) * scalar::kCount> scalars{};
    std::array<float, gfeat::kSize> global{};

    [[nodiscard]] int field(int tile, int f) const {
        return fields[(static_cast<std::size_t>(tile) * field::kCount) +
                      static_cast<std::size_t>(f)];
    }
    [[nodiscard]] float scalar(int tile, int s) const {
        return scalars[(static_cast<std::size_t>(tile) * scalar::kCount) +
                       static_cast<std::size_t>(s)];
    }
};

// The observer's features. Overwrites all of `out` (no allocation; Features is a value type).
void encode_features(const Observation& obs, Features& out);

// Where ADR-006 policy index `policy` sits in the network's output at map side `side`: the spatial
// head is [332][side][side] (channel-major), followed by the 130 global logits. Aborts if the
// index's tile is outside the map.
[[nodiscard]] int network_index(int policy, int side);

// Size of the network's policy output at `side`.
[[nodiscard]] constexpr int network_policy_size(int side) {
    return (kPolicyChannels * side * side) + kPolicyGlobal;
}

}  // namespace poly
