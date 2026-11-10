#include "search/root_groups.hpp"

#include <array>
#include <cstdint>

namespace poly::search {

namespace {

enum class Group : std::uint8_t { City, Unit, Tribe };

Group group_of(ActionType t) {
    switch (t) {
        case ActionType::EndTurn:
        case ActionType::ResearchTech:
        case ActionType::DeclareWar:
        case ActionType::SendStars:
        case ActionType::BuildRoad:
            return Group::Tribe;
        case ActionType::GatherResource:
        case ActionType::LevelUp:
        case ActionType::Spawn:
        case ActionType::ClearForest:
        case ActionType::GrowForest:
        case ActionType::BurnForest:
        case ActionType::Build:
        case ActionType::Destroy:
            return Group::City;
        case ActionType::Recover:
        case ActionType::Move:
        case ActionType::Examine:
        case ActionType::Capture:
        case ActionType::Attack:
        case ActionType::Disband:
        case ActionType::Upgrade:
        case ActionType::MakeVeteran:
        case ActionType::HealOthers:
        case ActionType::Convert:
            return Group::Unit;
    }
    return Group::Tribe;  // unreachable
}

bool candidate(Group g, const Action& a) {
    return group_of(a.type) == g && (g != Group::City || a.type != ActionType::Destroy);
}

}  // namespace

void root_candidates(std::span<const Action> legal, Rng& rng, std::vector<Action>& out) {
    out.clear();
    int city = 0;
    int unit = 0;
    int tribe = 0;
    for (const Action& a : legal) {
        switch (group_of(a.type)) {
            case Group::City:
                city += a.type != ActionType::Destroy ? 1 : 0;
                break;
            case Group::Unit:
                ++unit;
                break;
            case Group::Tribe:
                ++tribe;
                break;
        }
    }
    std::array<Group, 3> available{};
    std::uint32_t n = 0;
    if (city > 0) {
        available[n++] = Group::City;
    }
    if (unit > 0) {
        available[n++] = Group::Unit;
    }
    if (tribe > 1) {
        available[n++] = Group::Tribe;
    }
    if (n == 0) {
        return;
    }
    const Group g = available[rng.below(n)];
    for (const Action& a : legal) {
        if (candidate(g, a)) {
            out.push_back(a);
        }
    }
}

}  // namespace poly::search
