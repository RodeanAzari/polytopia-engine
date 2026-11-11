// Partial observability (rule facts 03b §1, 03a §5-6, ADR-004). The partial-obs replay difftest
// checks these against Tribes in whole games; these cover what its corpus doesn't reach (found by
// bug injection): road reveals, clear-view order, the explorer's network update, Eye of God, and a
// meeting draw that grants nothing. Expected values come from the rule sheet, not from our code.
// The Observation tests (04 §7) cover what the difftest's OBS text can't show: no slot left
// pointing at a hidden actor, and observations that don't change when only hidden information does.

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "poly/chance.hpp"
#include "poly/dump.hpp"
#include "poly/level.hpp"
#include "poly/network.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"

namespace {

using poly::GameState;
using poly::Slot;

// Records meeting draws (always index 0) and plays scripted explorer steps (then 0). Anything
// else fails the test.
class ScriptedChance final : public poly::Chance {
   public:
    std::vector<int> meeting_counts;
    std::deque<int> explorer_steps;

    poly::RuinBonus ruin_bonus(bool /*exclude_research*/) override {
        ADD_FAILURE() << "unexpected ruin draw";
        return poly::RuinBonus::Resources;
    }
    int random_tech(int /*count*/) override {
        ADD_FAILURE() << "unexpected random tech draw";
        return 0;
    }
    int meeting_tech(int count) override {
        meeting_counts.push_back(count);
        return 0;
    }
    int explorer_neighbour(int /*count*/) override {
        if (explorer_steps.empty()) {
            return 0;
        }
        const int v = explorer_steps.front();
        explorer_steps.pop_front();
        return v;
    }
    void shuffle_cities(std::span<std::uint16_t> /*cities*/) override {}
};

// An n x n level, plain unless overridden: {{x, y}, "cell"}.
std::vector<std::string> level(
    int n, const std::vector<std::pair<std::pair<int, int>, std::string>>& cells) {
    std::vector<std::vector<std::string>> grid(
        static_cast<std::size_t>(n), std::vector<std::string>(static_cast<std::size_t>(n), "."));
    for (const auto& [pos, cell] : cells) {
        grid[static_cast<std::size_t>(pos.first)][static_cast<std::size_t>(pos.second)] = cell;
    }
    std::vector<std::string> lines;
    for (const auto& row : grid) {
        std::string l;
        for (std::size_t i = 0; i < row.size(); ++i) {
            l += (i > 0 ? "," : "") + row[i];
        }
        lines.push_back(l);
    }
    return lines;
}

GameState load(const std::vector<std::pair<std::pair<int, int>, std::string>>& cells,
               poly::ObsMode obs = poly::ObsMode::Partial) {
    ScriptedChance chance;  // the cities below are too far apart to meet at load
    return poly::load_level(level(11, cells), poly::GameMode::Capitals, obs, chance);
}

std::size_t tile(const GameState& s, int x, int y) {
    return static_cast<std::size_t>(s.index(x, y));
}

void teleport(GameState& s, Slot u, int x, int y) {
    poly::Unit& unit = s.units[u];
    s.unit_at[tile(s, unit.x, unit.y)] = poly::kNoSlot;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    s.unit_at[tile(s, x, y)] = u;
}

Slot capital_unit(const GameState& s, int tribe) {
    return s.cities[s.tribes[static_cast<std::size_t>(tribe)].capital].units[0];
}

void grant(poly::TribeState& t, poly::Tech tech) { t.techs |= 1U << static_cast<unsigned>(tech); }

// 03b §1: revealing a road (network flag, not water, not city) or a water tile requests a network
// update; revealing other land, or an already visible road, does not.
TEST(Fog, RevealOfRoadOrWaterRequestsNetworkUpdate) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}, {{5, 8}, "s"}});
    ScriptedChance chance;
    EXPECT_FALSE(poly::clear_view(s, 0, 5, 5, 0, chance));  // hidden plain, no road
    s.network.set(tile(s, 5, 6));
    EXPECT_TRUE(poly::clear_view(s, 0, 5, 6, 0, chance));   // hidden road
    EXPECT_FALSE(poly::clear_view(s, 0, 5, 6, 0, chance));  // already visible
    EXPECT_TRUE(poly::clear_view(s, 0, 5, 8, 0, chance));   // hidden water
    EXPECT_EQ(s.tribes[0].visible.count(),
              16U + 3U);  // load view r2 clipped at the edge (4x4), + 3
}

// 03b §1: the neighbourhood is viewed before the centre, so a neighbour's tribe is met first.
TEST(Fog, ClearViewMeetsNeighboursBeforeTheCentre) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 1}, "c:2"}, {{1, 9}, "c:3"}});
    teleport(s, capital_unit(s, 1), 5, 5);  // centre
    teleport(s, capital_unit(s, 2), 4, 4);  // first neighbour in x-major order
    ScriptedChance chance;
    (void)poly::clear_view(s, 0, 5, 5, 1, chance);
    const auto& met = s.tribes[0].met;
    ASSERT_EQ(met.size(), 3U);
    EXPECT_EQ(met[1], 2);
    EXPECT_EQ(met[2], 1);
}

// 03b §1 quirk: the meeting tech is drawn among techs the other tribe has and P lacks, but granted
// only if researchable. Smithery without Mining: drawn (one candidate), not granted.
TEST(Fog, MeetingTechIsDrawnButGrantedOnlyIfResearchable) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    s.tribes[0].techs = 0;
    s.tribes[1].techs = 0;
    grant(s.tribes[1], poly::Tech::Smithery);
    teleport(s, capital_unit(s, 1), 5, 5);
    ScriptedChance chance;
    (void)poly::clear_view(s, 0, 5, 5, 0, chance);
    EXPECT_EQ(chance.meeting_counts, (std::vector<int>{1}));
    EXPECT_FALSE(s.tribes[0].has_tech(poly::Tech::Smithery));

    GameState r = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});  // Climbing (a root): granted
    r.tribes[0].techs = 0;
    r.tribes[1].techs = 0;
    grant(r.tribes[1], poly::Tech::Climbing);
    teleport(r, capital_unit(r, 1), 5, 5);
    (void)poly::clear_view(r, 0, 5, 5, 0, chance);
    EXPECT_TRUE(r.tribes[0].has_tech(poly::Tech::Climbing));
    EXPECT_EQ(r.tribes[0].score, s.tribes[0].score);  // no score for a meeting tech
}

// 03a §5: Eye of God becomes available when the last tile is revealed, under partial obs only.
TEST(Fog, EyeOfGodWhenEveryTileIsVisible) {
    const auto eye = [](GameState& g) {
        return poly::monument(g.tribes[0], poly::Building::EyeOfGod);
    };
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    ScriptedChance chance;
    for (int x = 0; x < 11; ++x) {  // every tile but (10, 10)
        for (int y = 0; y < 11; ++y) {
            if (x != 10 || y != 10) {
                (void)poly::clear_view(s, 0, x, y, 0, chance);
            }
        }
    }
    EXPECT_EQ(s.tribes[0].visible.count(), 120U);
    EXPECT_EQ(eye(s), poly::MonumentStatus::Unavailable);
    (void)poly::clear_view(s, 0, 10, 10, 0, chance);
    EXPECT_EQ(eye(s), poly::MonumentStatus::Available);

    GameState f = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}}, poly::ObsMode::Full);
    (void)poly::clear_view(f, 0, 5, 5, 5, chance);
    EXPECT_EQ(eye(f), poly::MonumentStatus::Unavailable);
}

// 03b §6 / Tribes' Board.launchExplorer: an explorer revealing water recomputes the tribe's
// network. Ports at (2,2) (capital) and (2,5) (second city) are 3 water steps apart, but (2,4) is
// hidden, so they are not linked until the explorer reveals it.
TEST(Fog, ExplorerRevealUpdatesTheNetwork) {
    GameState s = load({{{1, 1}, "c:0"},
                        {{9, 9}, "c:2"},
                        {{1, 6}, "v"},
                        {{2, 2}, "s"},
                        {{2, 3}, "s"},
                        {{2, 4}, "s"},
                        {{2, 5}, "s"}});
    ScriptedChance chance;
    poly::start_turn(s, 0);
    const Slot w = capital_unit(s, 0);
    teleport(s, w, 1, 6);
    poly::apply_action(s, {.type = poly::ActionType::Capture, .actor = s.units[w].id, .target = 0},
                       chance);
    ASSERT_EQ(s.tribes[0].cities.size(), 2U);
    const Slot second = s.tribes[0].cities[1];
    for (const auto& [x, y] : {std::pair{2, 2}, std::pair{2, 5}}) {
        s.building[tile(s, x, y)] = poly::Building::Port;
        s.network.set(tile(s, x, y));
    }
    poly::recompute_network_tribe(s, 0);
    ASSERT_FALSE(s.tribes[0].visible.test(tile(s, 2, 4)));
    ASSERT_FALSE(s.tribes[0].connected.contains(second));

    grant(s.tribes[0], poly::Tech::Sailing);  // the explorer may enter shallow water
    s.cities[s.tribes[0].capital].population = 2;
    chance.explorer_steps = {7, 4};  // (1,1) -> (2,2) -> (2,3), which reveals (2,4)
    poly::apply_action(s,
                       {.type = poly::ActionType::LevelUp,
                        .actor = s.cities[s.tribes[0].capital].id,
                        .param = static_cast<std::uint8_t>(poly::LevelUpBonus::Explorer)},
                       chance);
    EXPECT_TRUE(s.tribes[0].visible.test(tile(s, 2, 4)));
    EXPECT_TRUE(s.tribes[0].connected.contains(second));
}

// 04 §7: a tile the observer can't see shows fog and nothing else, except that it is owned (Tribes'
// city id 0, not -1) by no actual city: our phantom ghost (kFogOwnerId, tribe -1).
TEST(Observation, FogTilesHoldNothing) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}, {{8, 8}, "d:w"}});
    s.network.set(tile(s, 8, 8));
    ASSERT_NE(s.unit_at[tile(s, 9, 9)], poly::kNoSlot);
    const poly::Observation obs = poly::observe(s, 0);
    const GameState& o = obs.view();
    const auto bare_fog = [&](std::size_t i) {
        return o.terrain[i] == poly::Terrain::Fog && o.resource[i] == poly::Resource::None &&
               o.building[i] == poly::Building::None && o.unit_at[i] == poly::kNoSlot &&
               o.city_owner[i] != poly::kNoSlot &&
               o.cities[o.city_owner[i]].id == poly::kFogOwnerId &&
               o.cities[o.city_owner[i]].tribe == -1 && !o.network.test(i);
    };
    int fog = 0;
    for (std::size_t i = 0; i < 121; ++i) {
        if (!s.tribes[0].visible.test(i)) {
            ++fog;
            EXPECT_TRUE(bare_fog(i)) << "tile " << i;
        }
    }
    EXPECT_EQ(fog, 121 - 16);
}

// 04 §7: a hidden enemy unit is gone; a hidden enemy capital is a ghost that keeps only its id
// (tribe 1's capital id stays visible), not even its position or level.
TEST(Observation, HiddenActors) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    const Slot cap = s.tribes[1].capital;
    s.cities[cap].level = 3;
    const poly::Observation obs = poly::observe(s, 0);
    const GameState& o = obs.view();
    EXPECT_EQ(o.units[capital_unit(s, 1)].id, 0);
    EXPECT_EQ(o.tribes[1].capital, cap);
    EXPECT_EQ(o.cities[cap].id, s.cities[cap].id);
    EXPECT_EQ(o.cities[cap].tribe, -1);
    EXPECT_EQ(o.cities[cap].x + o.cities[cap].y, 0);
    EXPECT_EQ(o.cities[cap].level, 1);
    EXPECT_TRUE(o.tribes[1].cities.empty());
}

// 04 §7: crops need Organization, ore Climbing, whales Fishing: the observer's techs, not the
// owner's.
TEST(Observation, ResourcesNeedTheObserversTech) {
    GameState s =
        load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}, {{0, 2}, ".:c"}, {{2, 0}, "m:o"}, {{3, 3}, "d:w"}});
    s.tribes[0].techs = 0;
    s.tribes[1].techs = ~0U;
    const std::array<std::pair<int, int>, 3> at = {{{0, 2}, {2, 0}, {3, 3}}};
    for (const auto& [x, y] : at) {
        EXPECT_EQ(poly::observe(s, 0).view().resource[tile(s, x, y)], poly::Resource::None);
    }
    grant(s.tribes[0], poly::Tech::Organization);
    grant(s.tribes[0], poly::Tech::Climbing);
    grant(s.tribes[0], poly::Tech::Fishing);
    const poly::Observation obs = poly::observe(s, 0);
    const GameState& o = obs.view();
    EXPECT_EQ(o.resource[tile(s, 0, 2)], poly::Resource::Crops);
    EXPECT_EQ(o.resource[tile(s, 2, 0)], poly::Resource::Ore);
    EXPECT_EQ(o.resource[tile(s, 3, 3)], poly::Resource::Whales);
}

// 04 §7: a visible enemy unit is listed among its tribe's units, with no home city and no kills.
TEST(Observation, VisibleEnemyUnit) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    const Slot u = capital_unit(s, 1);
    teleport(s, u, 2, 2);
    s.units[u].kills = 2;
    const poly::Observation obs = poly::observe(s, 0);
    const GameState& o = obs.view();
    ASSERT_EQ(o.tribes[1].tribe_units.size(), 1U);
    EXPECT_EQ(o.tribes[1].tribe_units[0], u);
    EXPECT_EQ(o.units[u].city, poly::kNoSlot);
    EXPECT_EQ(o.units[u].kills, 0);
    EXPECT_EQ(o.units[u].hp, s.units[u].hp);
    EXPECT_EQ(o.unit_at[tile(s, 2, 2)], u);
}

// No peeking (CLAUDE.md): two states that differ only in what 04 §7 hides give the same
// observation, compared as the OBS text and as the raw tile-to-actor slots the rules read.
TEST(Observation, HiddenInformationDoesNotChangeIt) {
    GameState a = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}, {{7, 7}, "m"}});
    const Slot u = capital_unit(a, 1);
    teleport(a, u, 9, 8);
    GameState b = a;
    poly::TribeState& t = b.tribes[1];
    t.stars += 7;
    t.kills = 3;
    t.pacifist_count = 2;
    grant(t, poly::Tech::Sailing);
    poly::monument(t, poly::Building::EyeOfGod) = poly::MonumentStatus::Available;
    poly::City& cap = b.cities[t.capital];
    cap.population += 1;
    cap.production_bonus += 1;
    cap.level += 1;
    cap.points_worth += 50;
    b.cities[b.tribes[0].capital].points_worth += 50;  // not carried for own cities either
    teleport(b, u, 8, 8);                              // a hidden unit moves inside the fog
    b.terrain[tile(b, 7, 7)] = poly::Terrain::Plain;   // hidden terrain changes

    const auto same = [](const GameState& x, const GameState& y) {
        const poly::Observation ox = poly::observe(x, 0);
        const poly::Observation oy = poly::observe(y, 0);
        return poly::dump_observation(ox) == poly::dump_observation(oy) &&
               ox.view().unit_at == oy.view().unit_at &&
               ox.view().city_owner == oy.view().city_owner;
    };
    EXPECT_TRUE(same(a, b));
    b.tribes[1].score += 1;  // visible: the comparison can tell
    EXPECT_FALSE(same(a, b));
}

long fog_owners(const GameState& s) {
    return std::ranges::count(s.cities, poly::kFogOwnerId, &poly::City::id);
}

long ghosts(const GameState& s) {
    return std::ranges::count_if(s.cities, [](const poly::City& c) {
        return c.id != 0 && c.id != poly::kFogOwnerId && c.tribe < 0;
    });
}

// Search observes its own nodes (ADR-008 §3: the mover's observation of a state stepped from an
// observation), so observe must accept an observation: ghost cities stay ghosts, the fog owner is
// reused rather than duplicated, and observing again for the same tribe changes nothing.
TEST(Observation, ObservingAnObservation) {
    const GameState a = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    const GameState once = poly::observe(a, 0).view();
    ASSERT_EQ(fog_owners(once), 1);
    ASSERT_EQ(ghosts(once), 1);  // tribe 1's capital, in tribe 0's fog
    const poly::Observation twice = poly::observe(once, 0);
    EXPECT_EQ(poly::dump_observation(twice), poly::dump_observation(poly::observe(a, 0)));
    EXPECT_EQ(fog_owners(twice.view()), 1);
    EXPECT_EQ(ghosts(twice.view()), 1);
    const poly::Observation other = poly::observe(once, 1);  // the opponent's view of tribe 0's
    EXPECT_EQ(fog_owners(other.view()), 1);
    EXPECT_EQ(ghosts(other.view()), 1);
}

// 04 §7: Tribes' actor map (a HashMap, 16 buckets) doubles once it holds more than 3/4 of its
// bucket count in live cities + units, and never shrinks.
TEST(Observation, ActorMapCapacityGrowsAtThreeQuarters) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    const auto live = [&] {
        return std::ranges::count_if(s.cities, [](const poly::City& c) { return c.id != 0; }) +
               std::ranges::count_if(s.units, [](const poly::Unit& u) { return u.id != 0; });
    };
    ASSERT_EQ(s.actor_map_capacity, 16);
    while (live() < 12) {
        (void)poly::new_unit(s);
    }
    EXPECT_EQ(s.actor_map_capacity, 16);  // 12 = 3/4 of 16: no resize yet
    const Slot u = poly::new_unit(s);
    EXPECT_EQ(s.actor_map_capacity, 32);
    s.units[u] = poly::Unit{};  // removing an actor never shrinks it
    (void)poly::new_unit(s);
    EXPECT_EQ(s.actor_map_capacity, 32);
}

// 04 §7: another tribe's visible units are listed in the actor map's iteration order, bucket
// (id mod capacity) first, so it depends on the capacity, not just on the ids.
TEST(Observation, ForeignActorsInActorMapOrder) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    s.tribes[0].visible.set();  // the observer sees everything
    const Slot a = capital_unit(s, 1);
    const Slot b = poly::new_unit(s);
    s.units[b] = s.units[a];
    teleport(s, b, 9, 8);
    s.units[a].id = 20;  // bucket 4 of 16, 20 of 32
    s.units[b].id = 6;   // bucket 6
    const auto listed = [&](int capacity) {
        s.actor_map_capacity = capacity;
        const poly::Observation obs = poly::observe(s, 0);
        std::vector<std::int32_t> ids;
        for (const Slot u : obs.view().tribes[1].tribe_units) {
            ids.push_back(obs.view().units[u].id);
        }
        return ids;
    };
    EXPECT_EQ(listed(16), (std::vector<std::int32_t>{20, 6}));
    EXPECT_EQ(listed(32), (std::vector<std::int32_t>{6, 20}));
}

// 04 §7, 03b §2: a fogged tile is owned by no actual city (Tribes' id 0), so claiming borders from
// an observation (a search, 04 §9) skips it while still taking visible unowned tiles.
TEST(Observation, FoggedTilesCannotBeClaimed) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    ASSERT_FALSE(s.tribes[0].visible.test(tile(s, 4, 4)));
    ASSERT_TRUE(s.tribes[0].visible.test(tile(s, 3, 3)));
    GameState v = poly::observe(s, 0).view();
    const Slot fog_owner = v.city_owner[tile(v, 4, 4)];
    const Slot capital = v.tribes[0].capital;
    poly::claim_tiles(v, capital, 3);
    EXPECT_EQ(v.city_owner[tile(v, 4, 4)], fog_owner);
    EXPECT_EQ(v.city_owner[tile(v, 3, 3)], capital);
}

// 04 §7, 03a §5: another tribe's monument map is empty in an observation, so in a search its
// unlocks (which need "unavailable") do nothing; building one still records it as built.
TEST(Observation, HiddenMonumentsDontUnlock) {
    GameState s = load({{{1, 1}, "c:0"}, {{9, 9}, "c:2"}});
    GameState v = poly::observe(s, 0).view();
    poly::unlock(v.tribes[1], poly::Building::ParkOfFortune);
    EXPECT_EQ(poly::monument(v.tribes[1], poly::Building::ParkOfFortune),
              poly::MonumentStatus::Absent);
    poly::unlock(v.tribes[0], poly::Building::ParkOfFortune);  // the observer's own map is real
    EXPECT_EQ(poly::monument(v.tribes[0], poly::Building::ParkOfFortune),
              poly::MonumentStatus::Available);
}

}  // namespace
