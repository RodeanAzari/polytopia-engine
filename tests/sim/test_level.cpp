// Map load (rule facts 05 §1, with 03b §1-2 for clear view and claims). Expected values are worked
// out from the rule sheet, not from our code. Paths the oracle corpus rarely or never reaches
// (overlapping borders, meeting another tribe at load, board edges) are covered here.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "poly/dump.hpp"
#include "poly/level.hpp"

namespace {

using poly::GameState;
using poly::ObsMode;

// An n x n plain level with city cells "c:<key>" at the given (x, y) positions.
std::vector<std::string> level(int n,
                               const std::vector<std::pair<std::pair<int, int>, int>>& cities) {
    std::vector<std::vector<std::string>> cells(
        static_cast<std::size_t>(n), std::vector<std::string>(static_cast<std::size_t>(n), "."));
    for (const auto& [pos, key] : cities) {
        cells[static_cast<std::size_t>(pos.first)][static_cast<std::size_t>(pos.second)] =
            "c:" + std::to_string(key);
    }
    std::vector<std::string> lines;
    for (const auto& row : cells) {
        std::string l;
        for (std::size_t i = 0; i < row.size(); ++i) {
            l += (i > 0 ? "," : "") + row[i];
        }
        lines.push_back(l);
    }
    return lines;
}

GameState load(const std::vector<std::string>& lines) {
    return poly::load_level(lines, poly::GameMode::Capitals, ObsMode::Full);
}

const poly::City& capital(const GameState& s, int tribe) {
    return s.cities[s.tribes[static_cast<std::size_t>(tribe)].capital];
}

std::vector<int> met(const GameState& s, int tribe) {
    std::vector<int> out;
    for (auto m : s.tribes[static_cast<std::size_t>(tribe)].met) {
        out.push_back(m);
    }
    return out;
}

constexpr int kXinXi = 0;
constexpr int kBardur = 2;
constexpr int kLuxidoor = 6;
constexpr int kVengir = 7;

// 05 §1.2: tribe id = row-major (x, then y) rank of the city, not the requested order. The
// positions are chosen so row-major and column-major orders disagree.
TEST(MapLoad, TribeIdsFollowRowMajorCityOrder) {
    const GameState s = load(level(11, {{{8, 2}, kXinXi}, {{2, 8}, kBardur}}));
    ASSERT_EQ(s.num_tribes, 2);
    EXPECT_EQ(s.tribes[0].type, poly::TribeType::Bardur);  // x = 2 comes before x = 8
    EXPECT_EQ(s.tribes[1].type, poly::TribeType::XinXi);
    EXPECT_EQ(capital(s, 0).x, 2);
    EXPECT_EQ(capital(s, 0).y, 8);
}

// 04 §1.1: one counter, incremented before use; per city: city id, then its unit's id.
TEST(MapLoad, ActorIdsInterleaveCityThenUnit) {
    const GameState s = load(level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}}));
    EXPECT_EQ(capital(s, 0).id, 1);
    EXPECT_EQ(s.units[capital(s, 0).units[0]].id, 2);
    EXPECT_EQ(capital(s, 1).id, 3);
    EXPECT_EQ(s.units[capital(s, 1).units[0]].id, 4);
    EXPECT_EQ(s.next_actor_id, 5);
}

// 05 §1.3 resulting score: tech tier x 100 + 100 centre + 20 per claimed tile + starting unit's
// points.
TEST(MapLoad, StartingScoresAndUnits) {
    const GameState s = load(level(11, {{{2, 2}, kXinXi}, {{8, 8}, kVengir}}));
    EXPECT_EQ(s.tribes[0].score, 100 + 100 + (9 * 20) + 10);  // Climbing tier 1, Warrior
    EXPECT_EQ(s.tribes[1].score, 300 + 100 + (9 * 20) + 25);  // Smithery tier 3, Swordsman
    const poly::Unit& u = s.units[capital(s, 1).units[0]];
    EXPECT_EQ(u.type, poly::UnitType::Swordsman);
    EXPECT_EQ(u.hp, 15);
    EXPECT_EQ(u.max_hp, 15);
    EXPECT_EQ(u.status, poly::TurnStatus::Finished);
    EXPECT_EQ(s.tribes[1].stars, 5);
    // Starting tech alone, without its parent (01): Smithery but not Mining.
    EXPECT_TRUE(s.tribes[1].has_tech(poly::Tech::Smithery));
    EXPECT_FALSE(s.tribes[1].has_tech(poly::Tech::Mining));
}

// 05 §1.3 step 2: Luxidoor's capital starts at level 3, population 0, needs 4, walls; no tech
// score.
TEST(MapLoad, LuxidoorCapitalStartsAtLevelThreeWithWalls) {
    const GameState s = load(level(11, {{{2, 2}, kLuxidoor}, {{8, 8}, kXinXi}}));
    const poly::City& c = capital(s, 0);
    EXPECT_EQ(c.level, 3);
    EXPECT_EQ(c.population, 0);
    EXPECT_EQ(c.population_need, 4);
    EXPECT_TRUE(c.walls);
    EXPECT_EQ(c.border_radius, 1);
    EXPECT_EQ(s.tribes[0].score, 0 + 100 + (9 * 20) + 10);
}

// 03b §2: claims never take owned tiles, so the earlier city (row-major) keeps the overlap.
TEST(MapLoad, OverlappingBordersStayWithTheEarlierCity) {
    const GameState s = load(level(11, {{{2, 2}, kXinXi}, {{2, 4}, kBardur}}));
    for (int x = 1; x <= 3; ++x) {
        EXPECT_EQ(s.city_owner[static_cast<std::size_t>(s.index(x, 3))], s.tribes[0].capital) << x;
    }
    EXPECT_EQ(capital(s, 0).points_worth, 9 * 20);
    EXPECT_EQ(capital(s, 1).points_worth, 6 * 20);
    EXPECT_EQ(s.tribes[1].score, 100 + 100 + (6 * 20) + 10);  // Hunting tier 1
}

// Claims are clipped to the board: a corner city owns 3 neighbours + itself.
TEST(MapLoad, ClaimsAreClippedAtTheBoardEdge) {
    const GameState s = load(level(11, {{{0, 0}, kXinXi}, {{8, 8}, kBardur}}));
    EXPECT_EQ(capital(s, 0).points_worth, 4 * 20);
}

// 03b §1 meeting quirks at load:
// - The second city's radius-1 view (before its own tiles are claimed) sees the first city's
// borders and
//   meets tribe 0 first; its later radius-2 view sees itself, but a non-empty list rejects the
//   tribe itself.
// - Tribe 0's radius-2 view reaches its own border tile (1,1) before tribe 1's (1,4), x-major
// order.
// - Meetings are one-directional.
TEST(MapLoad, MeetingOrderAndSelfMeetingQuirk) {
    const GameState s = load(level(11, {{{2, 2}, kXinXi}, {{2, 4}, kBardur}}));
    EXPECT_EQ(met(s, 0), (std::vector<int>{0, 1}));
    EXPECT_EQ(met(s, 1), (std::vector<int>{0}));
}

TEST(MapLoad, FarApartTribesOnlyMeetThemselves) {
    const GameState s = load(level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}}));
    EXPECT_EQ(met(s, 0), (std::vector<int>{0}));
    EXPECT_EQ(met(s, 1), (std::vector<int>{1}));
}

// Full obs (ADR-004): every tile visible, no reveal score. Network flag on city tiles only.
TEST(MapLoad, FullObsVisibilityAndNetwork) {
    const GameState s = load(level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}}));
    EXPECT_EQ(s.tribes[0].visible.count(), 121U);
    EXPECT_EQ(s.network.count(), 2U);
    EXPECT_TRUE(s.network.test(static_cast<std::size_t>(s.index(2, 2))));
    EXPECT_EQ(s.active_tribe, -1);
}

// 05 §1.1 cell format: resource from the first extra char, anything else = none; city cells carry
// none.
TEST(MapLoad, CellParsing) {
    auto lines = level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}});
    lines[0] = "d:w,s:h,m:o,f:a,v: ,.:z,.:,.:c,.:f,.:r,d:";
    const GameState s = load(lines);
    using poly::Resource;
    using poly::Terrain;
    EXPECT_EQ(s.terrain[0], Terrain::DeepWater);
    EXPECT_EQ(s.resource[0], Resource::Whales);
    EXPECT_EQ(s.resource[1], Resource::Fish);
    EXPECT_EQ(s.resource[2], Resource::Ore);
    EXPECT_EQ(s.resource[3], Resource::Animal);
    EXPECT_EQ(s.terrain[4], Terrain::Village);
    EXPECT_EQ(s.resource[4], Resource::None);
    EXPECT_EQ(s.resource[5], Resource::None);  // unknown resource char
    EXPECT_EQ(s.resource[6], Resource::None);
    EXPECT_EQ(s.resource[7], Resource::Crops);
    EXPECT_EQ(s.resource[9], Resource::Ruins);
    EXPECT_EQ(s.terrain[static_cast<std::size_t>(s.index(2, 2))], Terrain::City);
    EXPECT_EQ(s.resource[static_cast<std::size_t>(s.index(2, 2))], Resource::None);
}

TEST(MapLoad, TwoDigitTribeKey) {
    const GameState s = load(level(11, {{{2, 2}, 10}, {{8, 8}, 11}}));
    EXPECT_EQ(s.tribes[0].type, poly::TribeType::Quetzali);
    EXPECT_EQ(s.tribes[1].type, poly::TribeType::Yadakk);
}

// Invalid input fails loudly instead of producing a half-built state.
TEST(MapLoad, RejectsInvalidLevels) {
    EXPECT_THROW(load(level(11, {{{2, 2}, kXinXi}, {{8, 8}, kXinXi}})),
                 std::invalid_argument);                                       // duplicate
    EXPECT_THROW(load(level(11, {{{2, 2}, kXinXi}})), std::invalid_argument);  // one tribe
    EXPECT_THROW(load(level(11, {{{2, 2}, kXinXi}, {{8, 8}, 12}})),
                 std::invalid_argument);  // bad key
    EXPECT_THROW(load(level(25, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}})),
                 std::invalid_argument);  // too big
    auto short_line = level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}});
    short_line[3] = ".,.,.";
    EXPECT_THROW(load(short_line), std::invalid_argument);
    auto bad_terrain = level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}});
    bad_terrain[0][0] = 'x';
    EXPECT_THROW(load(bad_terrain), std::invalid_argument);
    auto bad_sep = level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}});
    bad_sep[0] = ".w" + bad_sep[0].substr(1);
    EXPECT_THROW(load(bad_sep), std::invalid_argument);
    EXPECT_THROW(load({}), std::invalid_argument);
}

TEST(MapLoad, DumpIsDeterministicAndEndsWithEnd) {
    const auto lines = level(11, {{{2, 2}, kXinXi}, {{8, 8}, kBardur}});
    const std::string a = poly::dump_state(load(lines));
    EXPECT_EQ(a, poly::dump_state(load(lines)));
    EXPECT_EQ(a.substr(a.size() - 4), "\nEND");
}

}  // namespace
