#pragma once

// The simulator state (ADR-005): one fixed-capacity, trivially copyable value type.
// Actors live in slot pools; the board and every list store slot indices. Each actor also keeps its
// Tribes actor id (same counter rule, rule facts 04 §1.1), used only for logs and difftests.

#include <array>
#include <bitset>
#include <cstdint>
#include <type_traits>

#include "poly/inline_vector.hpp"
#include "poly/types.hpp"

namespace poly {

inline constexpr int kMaxSide = 24;
inline constexpr int kMaxTiles = kMaxSide * kMaxSide;
inline constexpr int kMaxTribes = 8;
inline constexpr int kMaxCities = 64;
inline constexpr int kMaxUnits = 512;
inline constexpr int kMaxCityUnits = 16;      // a city holds level + 1 units
inline constexpr int kMaxCityBuildings = 25;  // one per tile within radius 2
inline constexpr int kMaxTribeUnits = 128;    // tribe-owned units (no city)

using Slot = std::uint16_t;
inline constexpr Slot kNoSlot = 0xFFFF;

struct Unit {
    std::int32_t id = 0;  // Tribes actor id; 0 = free slot
    UnitType type = UnitType::Warrior;
    std::int16_t tribe = -1;
    Slot city = kNoSlot;  // owning city, or kNoSlot for tribe-owned units
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::int16_t hp = 0;
    std::int16_t max_hp = 0;
    std::uint8_t kills = 0;
    bool veteran = false;
    TurnStatus status = TurnStatus::Fresh;
    UnitType carried = UnitType::Warrior;  // naval units only: the land unit inside
};

struct BuildingEntry {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    Building type = Building::None;
    std::uint8_t temple_level = 0;   // temples only
    std::int8_t turns_to_score = 0;  // temples only
};

struct City {
    std::int32_t id = 0;  // Tribes actor id; 0 = free slot
    std::int16_t tribe = -1;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::int16_t level = 1;
    std::int16_t population = 0;
    std::int16_t population_need = 2;
    std::int16_t production_bonus = 0;
    bool capital = false;
    bool walls = false;
    std::int16_t border_radius = 1;
    std::int32_t points_worth = 0;
    InlineVector<Slot, kMaxCityUnits> units;
    InlineVector<BuildingEntry, kMaxCityBuildings> buildings;
};

struct TribeState {
    TribeType type = TribeType::XinXi;
    std::uint32_t techs = 0;  // bit i = Tech i researched
    bool all_researched = false;
    std::int32_t stars = kStartingStars;
    std::int32_t score = 0;
    Result result = Result::Incomplete;
    Slot capital = kNoSlot;
    InlineVector<Slot, kMaxCities> cities;
    InlineVector<Slot, kMaxTribeUnits> tribe_units;
    InlineVector<std::int8_t, kMaxTribes> met;
    InlineVector<Slot, kMaxCities> connected;
    std::int32_t kills = 0;
    std::int32_t pacifist_count = 0;
    std::int32_t stars_sent_this_turn = 0;
    bool declared_war_this_turn = false;
    std::int32_t wars_declared = 0;
    std::int32_t stars_sent_total = 0;
    std::array<MonumentStatus, kNumMonuments> monuments{};
    std::bitset<kMaxTiles> visible;  // tile index x * side + y

    [[nodiscard]] bool has_tech(Tech t) const {
        return ((techs >> static_cast<unsigned>(t)) & 1U) != 0;
    }
};

struct GameState {
    // --- setup ---
    std::int16_t side = 0;
    std::int16_t num_tribes = 0;
    GameMode mode = GameMode::Capitals;
    ObsMode obs = ObsMode::Full;

    // --- turn bookkeeping ---
    std::int32_t tick = 0;
    std::int16_t active_tribe = -1;
    bool game_over = false;
    bool turn_must_end = false;
    bool leveling_up = false;
    std::uint8_t can_end_turn = 0;  // bit per tribe
    std::int32_t next_actor_id = 1;

    // --- board (structure of arrays, index x * side + y) ---
    std::array<Terrain, kMaxTiles> terrain{};
    std::array<Resource, kMaxTiles> resource{};
    std::array<Building, kMaxTiles> building{};
    std::array<Slot, kMaxTiles> unit_at{};
    std::array<Slot, kMaxTiles> city_owner{};
    std::bitset<kMaxTiles> network;

    // --- actors ---
    std::array<Unit, kMaxUnits> units{};
    std::array<City, kMaxCities> cities{};
    std::array<TribeState, kMaxTribes> tribes{};
    std::array<std::array<std::int16_t, kMaxTribes>, kMaxTribes> allegiance{};

    [[nodiscard]] int index(int x, int y) const { return (x * side) + y; }
    [[nodiscard]] bool on_board(int x, int y) const {
        return x >= 0 && y >= 0 && x < side && y < side;
    }
};

static_assert(std::is_trivially_copyable_v<GameState>, "ADR-005: GameState must copy with memcpy");

// ---- rule helpers used by map load and (later) actions ----

[[nodiscard]] inline MonumentStatus& monument(TribeState& t, Building b) {
    return t.monuments[static_cast<std::size_t>(static_cast<int>(b) - kFirstMonument)];
}

// Monument becomes available if it is still unavailable (03a §5).
inline void unlock(TribeState& t, Building b) {
    MonumentStatus& m = monument(t, b);
    if (m == MonumentStatus::Unavailable) {
        m = MonumentStatus::Available;
    }
}

// Add population v to city c, crediting tribe p (03a §1): floor at -level; p's score and c's
// points-worth move with the clamped change. p is not always c's owner (03a §6).
void add_population(GameState& s, City& c, int p, int v);

// Allocates a city slot with the next actor id (counter incremented before use, 04 §1.1).
Slot new_city(GameState& s);
// Allocates a unit slot with the next actor id.
Slot new_unit(GameState& s);

// Claim tiles (03b §2): neighbourhood(C, r) then C's tile; each unowned tile goes to C, +20
// score/worth.
void claim_tiles(GameState& s, Slot city, int radius);

// Clear view for tribe P at (x, y), radius r (03b §1): reveal (+5 per newly visible tile) and
// meetings. Full-obs mode only for now; partial obs needs the Chance interface (meeting tech draw).
void clear_view(GameState& s, int tribe, int x, int y, int radius);

}  // namespace poly
