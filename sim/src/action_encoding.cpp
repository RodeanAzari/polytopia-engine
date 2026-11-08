#include "poly/action_encoding.hpp"

#include <cstdlib>

namespace poly {

namespace {

struct Tile {
    int x = -1;
    int y = -1;
};

Tile unit_tile(const GameState& s, std::int32_t id) {
    for (const Unit& u : s.units) {
        if (u.id != 0 && u.id == id) {
            return {.x = u.x, .y = u.y};
        }
    }
    check(false, "action encoding: unit not in the view");
    return {};
}

Tile city_tile(const GameState& s, std::int32_t id) {
    for (const City& c : s.cities) {
        if (c.id != 0 && c.id == id) {
            return {.x = c.x, .y = c.y};
        }
    }
    check(false, "action encoding: city not in the view");
    return {};
}

// Offset channel within a (2r + 1)^2 window without its centre, x-major.
int offset(Tile from, Tile to, int radius) {
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    check(std::abs(dx) <= radius && std::abs(dy) <= radius && (dx != 0 || dy != 0),
          "action encoding: target outside the ADR-006 window");
    const int side = (2 * radius) + 1;
    const int k = ((dx + radius) * side) + (dy + radius);
    const int centre = (side * side) / 2;
    return k < centre ? k : k - 1;
}

int spatial(Tile t, int channel) {
    check(t.x >= 0 && t.y >= 0 && t.x < kMaxSide && t.y < kMaxSide,
          "action encoding: tile off the frame");
    return (((t.x * kMaxSide) + t.y) * kPolicyChannels) + channel;
}

int relative_tribe(const GameState& s, int tribe, int target) {
    check(target != tribe && target >= 0 && target < s.num_tribes,
          "action encoding: bad target tribe");
    return (target - tribe - 1 + s.num_tribes) % s.num_tribes;
}

constexpr int kGlobalBase = kPolicyTiles * kPolicyChannels;

}  // namespace

namespace detail {

int encode_in(const GameState& s, int tribe, const Action& a) {
    const Tile worked{.x = a.x, .y = a.y};
    switch (a.type) {
        case ActionType::EndTurn:
            return kGlobalBase + global::kEndTurn;
        case ActionType::ResearchTech:
            check(a.param < kNumTechs, "action encoding: bad tech");
            return kGlobalBase + global::kResearch + a.param;
        case ActionType::DeclareWar:
            return kGlobalBase + global::kDeclareWar + relative_tribe(s, tribe, a.target);
        case ActionType::SendStars:
            check(a.amount >= 1 && a.amount <= 14, "action encoding: stars outside 1..14");
            return kGlobalBase + global::kSendStars + (relative_tribe(s, tribe, a.target) * 14) +
                   (a.amount - 1);
        case ActionType::BuildRoad:
            return spatial(worked, channel::kBuildRoad);
        case ActionType::GatherResource:
            return spatial(worked, channel::kGather);
        case ActionType::ClearForest:
            return spatial(worked, channel::kClearForest);
        case ActionType::GrowForest:
            return spatial(worked, channel::kGrowForest);
        case ActionType::BurnForest:
            return spatial(worked, channel::kBurnForest);
        case ActionType::Destroy:
            return spatial(worked, channel::kDestroy);
        case ActionType::Build:
            check(a.param < 19, "action encoding: bad building");
            return spatial(worked, channel::kBuild + a.param);
        case ActionType::LevelUp:
            check(a.param < 8, "action encoding: bad level-up bonus");
            return spatial(city_tile(s, a.actor), channel::kLevelUp + a.param);
        case ActionType::Spawn:
            check(a.param < 12, "action encoding: bad unit type");
            return spatial(city_tile(s, a.actor), channel::kSpawn + a.param);
        case ActionType::Recover:
            return spatial(unit_tile(s, a.actor), channel::kRecover);
        case ActionType::Examine:
            return spatial(unit_tile(s, a.actor), channel::kExamine);
        case ActionType::Capture:
            return spatial(unit_tile(s, a.actor), channel::kCapture);
        case ActionType::Disband:
            return spatial(unit_tile(s, a.actor), channel::kDisband);
        case ActionType::Upgrade:
            return spatial(unit_tile(s, a.actor), channel::kUpgrade);
        case ActionType::MakeVeteran:
            return spatial(unit_tile(s, a.actor), channel::kMakeVeteran);
        case ActionType::HealOthers:
            return spatial(unit_tile(s, a.actor), channel::kHealOthers);
        case ActionType::Move: {
            const Tile from = unit_tile(s, a.actor);
            return spatial(from, channel::kMove + offset(from, worked, channel::kMoveRadius));
        }
        case ActionType::Attack: {
            const Tile from = unit_tile(s, a.actor);
            return spatial(from, channel::kAttack +
                                     offset(from, unit_tile(s, a.target), channel::kAttackRadius));
        }
        case ActionType::Convert: {
            const Tile from = unit_tile(s, a.actor);
            return spatial(from, channel::kConvert +
                                     offset(from, unit_tile(s, a.target), channel::kConvertRadius));
        }
    }
    check(false, "action encoding: unknown action type");
    return -1;
}

}  // namespace detail

int encode_action(const Observation& obs, const Action& a) {
    return detail::encode_in(obs.view(), obs.observer(), a);
}

std::optional<Action> decode_action(const Observation& obs, const ActionList& legal, int index) {
    for (const Action& a : legal) {
        if (encode_action(obs, a) == index) {
            return a;
        }
    }
    return std::nullopt;
}

}  // namespace poly
