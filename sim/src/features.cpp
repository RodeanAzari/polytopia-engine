#include "poly/features.hpp"

#include <span>

namespace poly {

namespace {

std::uint8_t none(int f) {
    return static_cast<std::uint8_t>(kTileClasses[static_cast<std::size_t>(f)] - 1);
}

// One tile's slices of Features.
struct TileOut {
    std::span<std::uint8_t, field::kCount> f;
    std::span<float, scalar::kCount> sc;
    std::span<std::uint8_t, 1> fl;  // the flag byte
};

// The tile's territory, and the city if the tile is one. Fogged tiles point at the observation's
// phantom fog owner, which says nothing: they keep "none".
template <typename Rel>
void encode_city(const GameState& s, int p, int t, const Rel& rel, TileOut& out) {
    const auto ti = static_cast<std::size_t>(t);
    const Slot c = s.city_owner[ti];
    if (c == kNoSlot || s.terrain[ti] == Terrain::Fog) {
        return;
    }
    const City& city = s.cities[c];
    out.f[field::kTerritory] = city.tribe >= 0 ? rel(city.tribe) : std::uint8_t{8};
    if (city.tribe < 0 || city.x != t / s.side || city.y != t % s.side) {
        return;  // owned territory, not the city itself
    }
    out.f[field::kCityOwner] = rel(city.tribe);
    if (city.capital) {
        out.fl[0] |= flag::kCapital;
    }
    if (city.walls) {
        out.fl[0] |= flag::kWalls;
    }
    out.sc[scalar::kCityLevel] = static_cast<float>(city.level) / 10.0F;
    out.sc[scalar::kCityBorder] = static_cast<float>(city.border_radius) / 3.0F;
    if (city.tribe == p && city.population_need > 0) {
        out.sc[scalar::kCityPopulation] =
            static_cast<float>(city.population) / static_cast<float>(city.population_need);
    }
}

template <typename Rel>
void encode_unit(const GameState& s, int t, const Rel& rel, TileOut& out) {
    const Slot u = s.unit_at[static_cast<std::size_t>(t)];
    if (u == kNoSlot) {
        return;
    }
    const Unit& unit = s.units[u];
    out.f[field::kUnitType] = static_cast<std::uint8_t>(unit.type);
    out.f[field::kUnitOwner] = rel(unit.tribe);
    out.f[field::kUnitStatus] = static_cast<std::uint8_t>(unit.status);
    if (unit.veteran) {
        out.fl[0] |= flag::kVeteran;
    }
    if (unit.max_hp > 0) {
        out.sc[scalar::kUnitHp] = static_cast<float>(unit.hp) / static_cast<float>(unit.max_hp);
    }
    if (unit.type == UnitType::Boat || unit.type == UnitType::Ship ||
        unit.type == UnitType::Battleship) {
        out.f[field::kCarried] = static_cast<std::uint8_t>(unit.carried);
    }
}

template <typename Rel>
void encode_global(const GameState& s, int p, const Rel& rel, std::array<float, gfeat::kSize>& g) {
    const TribeState& me = s.tribes[static_cast<std::size_t>(p)];
    const auto at = [&g](int k) -> float& { return g[static_cast<std::size_t>(k)]; };
    at(gfeat::kStars) = static_cast<float>(me.stars) / 20.0F;
    for (int k = 0; k < kNumTechs; ++k) {
        at(gfeat::kTechs + k) = me.has_tech(static_cast<Tech>(k)) ? 1.0F : 0.0F;
    }
    at(gfeat::kTribeType + static_cast<int>(me.type)) = 1.0F;
    for (int q = 0; q < s.num_tribes; ++q) {
        at(gfeat::kScore + rel(q)) =
            static_cast<float>(s.tribes[static_cast<std::size_t>(q)].score) / 1000.0F;
    }
    at(gfeat::kTick) = static_cast<float>(s.tick) / 50.0F;
    at(gfeat::kPartialObs) = s.obs == ObsMode::Partial ? 1.0F : 0.0F;
    at(gfeat::kScoreMode) = s.mode == GameMode::Score ? 1.0F : 0.0F;
    at(gfeat::kCanEndTurn) = ((s.can_end_turn >> static_cast<unsigned>(p)) & 1U) != 0 ? 1.0F : 0.0F;
    at(gfeat::kTurnMustEnd) = s.turn_must_end ? 1.0F : 0.0F;
    at(gfeat::kLevelingUp) = s.leveling_up ? 1.0F : 0.0F;
    at(gfeat::kNumTribes) = static_cast<float>(s.num_tribes) / 8.0F;
    for (int m = 0; m < kNumMonuments; ++m) {
        const int status = static_cast<int>(me.monuments[static_cast<std::size_t>(m)]);
        at(gfeat::kMonuments + (4 * m) + status) = 1.0F;
    }
}

}  // namespace

void encode_features(const Observation& obs, Features& out) {
    const GameState& s = obs.view();
    const int p = obs.observer();
    const int n = s.num_tribes;
    check(p >= 0 && p < n, "features: observer out of range");
    const auto rel = [p, n](int q) { return static_cast<std::uint8_t>((((q - p) % n) + n) % n); };
    const TribeState& me = s.tribes[static_cast<std::size_t>(p)];

    out.side = s.side;
    out.fields.fill(0);
    out.flags.fill(0);
    out.scalars.fill(0.0F);
    out.global.fill(0.0F);

    const int tiles = s.side * s.side;
    for (int t = 0; t < tiles; ++t) {
        const auto ti = static_cast<std::size_t>(t);
        TileOut o{.f = std::span(out.fields).subspan(ti * field::kCount).first<field::kCount>(),
                  .sc = std::span(out.scalars).subspan(ti * scalar::kCount).first<scalar::kCount>(),
                  .fl = std::span(out.flags).subspan(ti).first<1>()};
        for (int k = 0; k < field::kCount; ++k) {
            o.f[static_cast<std::size_t>(k)] = none(k);
        }
        o.f[field::kTerrain] = static_cast<std::uint8_t>(s.terrain[ti]);
        if (s.resource[ti] != Resource::None) {
            o.f[field::kResource] = static_cast<std::uint8_t>(s.resource[ti]);
        }
        if (s.building[ti] != Building::None) {
            o.f[field::kBuilding] = static_cast<std::uint8_t>(s.building[ti]);
        }
        if (me.visible.test(ti)) {
            o.fl[0] |= flag::kVisible;
        }
        if (s.network.test(ti)) {
            o.fl[0] |= flag::kNetwork;
        }
        encode_city(s, p, t, rel, o);
        encode_unit(s, t, rel, o);
    }
    encode_global(s, p, rel, out.global);
}

int network_index(int policy, int side) {
    check(policy >= 0 && policy < kPolicySize, "network_index: policy index out of range");
    check(side > 0 && side <= kMaxSide, "network_index: bad side");
    if (policy >= kPolicyTiles * kPolicyChannels) {
        return (kPolicyChannels * side * side) + (policy - (kPolicyTiles * kPolicyChannels));
    }
    const int tile = policy / kPolicyChannels;
    const int ch = policy % kPolicyChannels;
    const int x = tile / kMaxSide;
    const int y = tile % kMaxSide;
    check(x < side && y < side, "network_index: tile outside the map");
    return (((ch * side) + x) * side) + y;
}

}  // namespace poly
