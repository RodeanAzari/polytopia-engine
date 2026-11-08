// Action encoding (ADR-006). Expected indices are computed by hand from the ADR's table
// (tile t = 24x + y, index = t * 332 + channel; globals from 576 * 332 = 191,232), not from the
// code. The property test checks every legal action of random games, in both observability modes.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "poly/action_encoding.hpp"
#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"

namespace {

using poly::Action;
using poly::ActionType;
using poly::GameState;
using poly::Slot;

// An n x n plain level; `cells` overrides single cells, e.g. {{2, 2}, "c:0"}.
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

// Tribes 0..3 have capitals at (2, 2), (2, 13), (13, 2), (13, 13) on a 16 x 16 plain map.
GameState tribes(int count) {
    const std::array<std::pair<int, int>, 4> at = {{{2, 2}, {2, 13}, {13, 2}, {13, 13}}};
    std::vector<std::pair<std::pair<int, int>, std::string>> cells;
    cells.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        cells.emplace_back(at[static_cast<std::size_t>(i)], "c:" + std::to_string(i));
    }
    poly::SampledChance chance(1);
    return poly::load_level(level(16, cells), poly::GameMode::Capitals, poly::ObsMode::Full,
                            chance);
}

Slot capital_unit(const GameState& s, int tribe) {
    return s.cities[s.tribes[static_cast<std::size_t>(tribe)].capital].units[0];
}

void teleport(GameState& s, Slot u, int x, int y) {
    poly::Unit& unit = s.units[u];
    s.unit_at[static_cast<std::size_t>(s.index(unit.x, unit.y))] = poly::kNoSlot;
    unit.x = static_cast<std::uint8_t>(x);
    unit.y = static_cast<std::uint8_t>(y);
    s.unit_at[static_cast<std::size_t>(s.index(x, y))] = u;
}

int enc(const GameState& s, const Action& a) { return poly::detail::encode_in(s, 0, a); }

TEST(ActionEncoding, SizesMatchTheAdr) {
    EXPECT_EQ(poly::kPolicyChannels, 332);
    EXPECT_EQ(poly::kPolicySize, 191362);
}

TEST(ActionEncoding, GlobalActions) {
    GameState s = tribes(4);
    EXPECT_EQ(enc(s, {.type = ActionType::EndTurn, .actor = 0}), 191232);
    EXPECT_EQ(enc(s, {.type = ActionType::ResearchTech, .actor = 0, .param = 0}), 191233);
    EXPECT_EQ(enc(s, {.type = ActionType::ResearchTech, .actor = 0, .param = 23}), 191256);
    // Relative tribe r = (Q - P - 1) mod n: from tribe 0, Q = 1 -> 0, Q = 3 -> 2.
    EXPECT_EQ(enc(s, {.type = ActionType::DeclareWar, .actor = 0, .target = 1}), 191257);
    EXPECT_EQ(enc(s, {.type = ActionType::DeclareWar, .actor = 0, .target = 3}), 191259);
    // 191232 + 32 + 0 * 14 + (3 - 1)
    EXPECT_EQ(enc(s, {.type = ActionType::SendStars, .actor = 0, .target = 1, .amount = 3}),
              191266);
    // From tribe 2 to tribe 1: r = (1 - 2 - 1 + 4) mod 4 = 2; 191232 + 32 + 28 + 13
    EXPECT_EQ(poly::detail::encode_in(
                  s, 2, {.type = ActionType::SendStars, .actor = 2, .target = 1, .amount = 14}),
              191305);
    // From tribe 3 to tribe 0 wraps: r = (0 - 3 - 1 + 4) mod 4 = 0.
    EXPECT_EQ(
        poly::detail::encode_in(s, 3, {.type = ActionType::DeclareWar, .actor = 3, .target = 0}),
        191257);
}

TEST(ActionEncoding, UnitActionsSitOnTheUnitsTile) {
    GameState s = tribes(2);
    const Slot u = capital_unit(s, 0);
    teleport(s, u, 5, 5);  // tile 125, base 125 * 332 = 41500
    const std::int32_t id = s.units[u].id;
    EXPECT_EQ(enc(s, {.type = ActionType::Recover, .actor = id}), 41500);
    EXPECT_EQ(enc(s, {.type = ActionType::Capture, .actor = id}), 41502);
    EXPECT_EQ(enc(s, {.type = ActionType::HealOthers, .actor = id}), 41506);
    // Move (-1, -1): window index (6 * 15 + 6) = 96 < centre 112 -> channel 7 + 96
    EXPECT_EQ(enc(s, {.type = ActionType::Move, .actor = id, .x = 4, .y = 4}), 41603);
    // Move (0, +1): 7 * 15 + 8 = 113 > 112 -> 112 -> channel 119
    EXPECT_EQ(enc(s, {.type = ActionType::Move, .actor = id, .x = 5, .y = 6}), 41619);
    // Window corners (+7, +7) and (-7, -7): channels 7 + 223 = 230 and 7 + 0
    teleport(s, u, 8, 8);  // tile 200, base 66400
    EXPECT_EQ(enc(s, {.type = ActionType::Move, .actor = id, .x = 15, .y = 15}), 66630);
    EXPECT_EQ(enc(s, {.type = ActionType::Move, .actor = id, .x = 1, .y = 1}), 66407);
}

TEST(ActionEncoding, AttackAndConvertTargetTheVictimsOffset) {
    GameState s = tribes(2);
    const Slot u = capital_unit(s, 0);
    const Slot enemy = capital_unit(s, 1);
    teleport(s, u, 5, 5);
    const std::int32_t id = s.units[u].id;
    const std::int32_t target = s.units[enemy].id;
    // Attack (-3, +3): 0 * 7 + 6 = 6 < 24 -> channel 231 + 6
    teleport(s, enemy, 2, 8);
    EXPECT_EQ(enc(s, {.type = ActionType::Attack, .actor = id, .target = target}), 41737);
    // Convert (+1, -1): 2 * 3 + 0 = 6 > 4 -> 5 -> channel 279 + 5
    teleport(s, enemy, 6, 4);
    EXPECT_EQ(enc(s, {.type = ActionType::Convert, .actor = id, .target = target}), 41784);
}

TEST(ActionEncoding, CityActions) {
    GameState s = tribes(2);
    const std::int32_t city = s.cities[s.tribes[0].capital].id;  // at (2, 2): tile 50, base 16600
    EXPECT_EQ(enc(s, {.type = ActionType::Spawn, .actor = city, .param = 1}), 16896);
    EXPECT_EQ(enc(s, {.type = ActionType::LevelUp, .actor = city, .param = 5}), 16892);
    // Worked tile (3, 2): tile 74, base 24568
    EXPECT_EQ(enc(s, {.type = ActionType::GatherResource, .actor = city, .x = 3, .y = 2}), 24875);
    EXPECT_EQ(enc(s, {.type = ActionType::Build, .actor = city, .x = 3, .y = 2, .param = 3}),
              24884);
    EXPECT_EQ(enc(s, {.type = ActionType::Destroy, .actor = city, .x = 3, .y = 2}), 24879);
    EXPECT_EQ(enc(s, {.type = ActionType::BuildRoad, .actor = 0, .x = 5, .y = 5}), 41812);
    // A unit on its own city tile keeps unit channels: Recover at (2, 2) is the tile's channel 0.
    EXPECT_EQ(enc(s, {.type = ActionType::Recover, .actor = s.units[capital_unit(s, 0)].id}),
              16600);
}

TEST(ActionEncodingDeathTest, OutsideTheWindowFailsLoudly) {
    GameState s = tribes(2);
    const Slot u = capital_unit(s, 0);
    teleport(s, u, 8, 0);
    const std::int32_t id = s.units[u].id;
    EXPECT_DEATH((void)enc(s, {.type = ActionType::Move, .actor = id, .x = 0, .y = 0}), "window");
    EXPECT_DEATH((void)enc(s, {.type = ActionType::Move, .actor = id, .x = 8, .y = 0}), "window");
}

// Under fog an attack target the observer can't see isn't in its view: encoding from the
// observation fails loudly instead of peeking at the true state.
TEST(ActionEncodingDeathTest, HiddenTargetIsNotInTheObservation) {
    poly::SampledChance chance(1);
    GameState s = poly::load_level(level(16, {{{2, 2}, "c:0"}, {{13, 13}, "c:1"}}),
                                   poly::GameMode::Capitals, poly::ObsMode::Partial, chance);
    const Action attack{.type = ActionType::Attack,
                        .actor = s.units[capital_unit(s, 0)].id,
                        .target = s.units[capital_unit(s, 1)].id};
    const poly::Observation obs = poly::observe(s, 0);
    EXPECT_DEATH((void)poly::encode_action(obs, attack), "not in the view");
}

// ---- property test over random games ----

// One random cell: terrain by cumulative percentage, with that terrain's resource 1 time in `odds`.
std::string random_cell(poly::Rng& rng) {
    struct Kind {
        std::uint32_t below;  // cumulative percentage
        const char* bare;
        const char* with_resource;
        std::uint32_t odds;
    };
    static constexpr std::array<Kind, 5> kKinds = {
        {{.below = 40, .bare = ".", .with_resource = ".:f", .odds = 4},
         {.below = 58, .bare = "f", .with_resource = "f:a", .odds = 3},
         {.below = 68, .bare = "m", .with_resource = "m:o", .odds = 3},
         {.below = 82, .bare = "s", .with_resource = "s:h", .odds = 3},
         {.below = 92, .bare = "d", .with_resource = "d:w", .odds = 4}}};
    const std::uint32_t r = rng.below(100);
    for (const Kind& k : kKinds) {
        if (r < k.below) {
            return rng.below(k.odds) == 0 ? k.with_resource : k.bare;
        }
    }
    return r < 96 ? "v" : ".:r";  // village, or ruins on a plain
}

// A random 16 x 16 level: mixed terrain and resources, `players` capitals in the corners.
std::vector<std::string> random_level(poly::Rng& rng, int players) {
    const std::array<std::pair<int, int>, 4> caps = {{{2, 2}, {2, 13}, {13, 2}, {13, 13}}};
    std::vector<std::pair<std::pair<int, int>, std::string>> cells;
    cells.reserve(256 + static_cast<std::size_t>(players));
    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 16; ++y) {
            cells.emplace_back(std::pair{x, y}, random_cell(rng));
        }
    }
    for (int i = 0; i < players; ++i) {
        cells.emplace_back(caps[static_cast<std::size_t>(i)], "c:" + std::to_string(3 * i));
    }
    return level(16, cells);
}

struct Coverage {
    std::set<ActionType> types;
    long long actions = 0;

    [[nodiscard]] std::string missing() const {
        std::string out;
        for (int t = 0; t <= static_cast<int>(ActionType::Convert); ++t) {
            if (!types.contains(static_cast<ActionType>(t))) {
                out += std::to_string(t) + " ";
            }
        }
        return out;
    }
};

// What is wrong with one legal action's index, or "" if nothing: in range, unique within its set
// (`seen`), the same from the observation as from the true state, and decoded back to itself.
std::string problem(const GameState& s, const poly::Observation& o, const poly::ActionList& legal,
                    const Action& a, std::set<int>& seen) {
    const int k = poly::encode_action(o, a);
    if (k < 0 || k >= poly::kPolicySize) {
        return "index out of range: " + std::to_string(k);
    }
    if (!seen.insert(k).second) {
        return "collision at index " + std::to_string(k);
    }
    if (poly::detail::encode_in(s, o.observer(), a) != k) {
        return "true state encodes differently at index " + std::to_string(k);
    }
    if (poly::decode_action(o, legal, k) != a) {
        return "decode does not round-trip at index " + std::to_string(k);
    }
    return "";
}

// Every legal action of one decision.
void check_legal_set(const GameState& s, int p, const poly::ActionList& legal, Coverage& cov) {
    const poly::Observation o = poly::observe(s, p);
    std::set<int> seen;
    for (const Action& a : legal) {
        ASSERT_EQ(problem(s, o, legal, a, seen), "") << "action type " << static_cast<int>(a.type);
        cov.types.insert(a.type);
        ++cov.actions;
    }
}

// Coverage setup: every unit and most buildings available from the start, two techs left to
// research, and each starting unit one step from veteran.
void give_everything(GameState& s) {
    const std::uint32_t withheld = (1U << static_cast<unsigned>(poly::Tech::Trade)) |
                                   (1U << static_cast<unsigned>(poly::Tech::Aquatism));
    for (int p = 0; p < s.num_tribes; ++p) {
        s.tribes[static_cast<std::size_t>(p)].techs = ((1U << poly::kNumTechs) - 1) & ~withheld;
        s.units[capital_unit(s, p)].kills = 3;
    }
}

// One turn of random play for tribe p (at most 60 actions), checking every legal set.
void play_turn(GameState& s, int p, poly::Rng& rng, poly::Chance& chance, Coverage& cov) {
    poly::start_turn(s, p);
    s.tribes[static_cast<std::size_t>(p)].stars += 15;
    poly::ActionList legal;
    for (int step = 0; step < 60 && !s.game_over; ++step) {
        poly::legal_actions(s, legal);
        check_legal_set(s, p, legal, cov);
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
        const Action a = legal[static_cast<int>(rng.below(legal.size()))];
        if (a.type == ActionType::EndTurn) {
            break;
        }
        poly::apply_action(s, a, chance);
        poly::update_game_over(s);
    }
    poly::end_turn(s);
    poly::update_game_over(s);
}

void check_game(std::uint64_t seed, poly::ObsMode obs, Coverage& cov) {
    poly::Rng rng(seed);
    poly::SampledChance chance(seed);
    const int players = 2 + static_cast<int>(seed % 3);
    GameState s =
        poly::load_level(random_level(rng, players), poly::GameMode::Capitals, obs, chance);
    give_everything(s);
    while (!s.game_over && s.tick < 30 && !::testing::Test::HasFatalFailure()) {
        const int before = s.tick;
        const int p = poly::next_turn_tribe(s);
        if (s.tick != before && poly::update_game_over(s)) {
            break;
        }
        play_turn(s, p, rng, chance, cov);
    }
}

TEST(ActionEncoding, PropertyOverRandomGamesFullObs) {
    Coverage cov;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        check_game(seed, poly::ObsMode::Full, cov);
    }
    EXPECT_EQ(cov.types.size(), 23U) << "action types never exercised: " << cov.missing();
    EXPECT_GT(cov.actions, 100000);
}

TEST(ActionEncoding, PropertyOverRandomGamesPartialObs) {
    Coverage cov;
    for (std::uint64_t seed = 101; seed <= 112; ++seed) {
        check_game(seed, poly::ObsMode::Partial, cov);
    }
    EXPECT_EQ(cov.types.size(), 23U) << "action types never exercised: " << cov.missing();
    EXPECT_GT(cov.actions, 100000);
}

TEST(ActionEncoding, DecodeOfAnIndexNoLegalActionHasIsEmpty) {
    GameState s = tribes(2);
    poly::start_turn(s, 0);
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    const poly::Observation o = poly::observe(s, 0);
    EXPECT_EQ(poly::decode_action(o, legal, 0), std::nullopt);  // tile (0, 0): no unit or city
}

}  // namespace
