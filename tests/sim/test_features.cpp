// Network input features (ADR-011 §1). Expected values come from the ADR's field table and the enum
// orders in types.hpp (e.g. Terrain::City is class 5, UnitType::Warrior class 0), not from the
// code. The property tests play random games and check that features see only the observation
// (hidden changes don't move them, visible ones do), and that every legal action's policy index
// lands on a distinct slot of the network's output at the map's side.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "poly/action_encoding.hpp"
#include "poly/chance.hpp"
#include "poly/features.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"

namespace {

using poly::Features;
using poly::GameState;

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

// Two tribes on an 11 x 11 plain map: tribe 0 (Xin Xi, "c:0") at (2, 2), tribe 1 (Imperius,
// "c:3", tribe type 3) at (8, 8); a forest with animals at (2, 4).
GameState two_tribes(poly::ObsMode obs) {
    poly::SampledChance chance(1);
    return poly::load_level(level(11, {{{2, 2}, "c:0"}, {{8, 8}, "c:3"}, {{2, 4}, "f:a"}}),
                            poly::GameMode::Capitals, obs, chance);
}

Features features(const GameState& s, int p) {
    Features f;
    poly::encode_features(poly::observe(s, p), f);
    return f;
}

int tile(const GameState& s, int x, int y) { return s.index(x, y); }

bool same(const Features& a, const Features& b) {
    return a.side == b.side && a.fields == b.fields && a.flags == b.flags &&
           a.scalars == b.scalars && a.global == b.global;
}

TEST(Features, ClassCountsMatchTheAdr) {
    // Terrain 8 (Fog included), resources 7 + none, buildings 19 + none, territory r 0..7 +
    // unknown + none, city owner 8 + none, unit type 12 + none, unit owner 8 + none, status 6 +
    // none, carried 12 + none.
    EXPECT_EQ(poly::kTileClasses, (std::array<int, 9>{8, 8, 20, 10, 9, 13, 9, 7, 13}));
    EXPECT_EQ(poly::gfeat::kSize, 80);
}

TEST(Features, OwnCapitalTileFromTribeZero) {
    const GameState s = two_tribes(poly::ObsMode::Full);
    const Features f = features(s, 0);
    const int t = tile(s, 2, 2);
    EXPECT_EQ(f.side, 11);
    EXPECT_EQ(f.field(t, poly::field::kTerrain), 5);    // Terrain::City
    EXPECT_EQ(f.field(t, poly::field::kTerritory), 0);  // own
    EXPECT_EQ(f.field(t, poly::field::kCityOwner), 0);
    EXPECT_EQ(f.field(t, poly::field::kUnitType), 0);  // the starting warrior (Xin Xi)
    EXPECT_EQ(f.field(t, poly::field::kUnitOwner), 0);
    EXPECT_EQ(f.field(t, poly::field::kCarried), 12);  // none: not a naval unit
    EXPECT_NE(f.flags[static_cast<std::size_t>(t)] & poly::flag::kCapital, 0);
    EXPECT_EQ(f.flags[static_cast<std::size_t>(t)] & poly::flag::kWalls, 0);
    EXPECT_FLOAT_EQ(f.scalar(t, poly::scalar::kCityLevel), 0.1F);  // level 1 / 10
    EXPECT_FLOAT_EQ(f.scalar(t, poly::scalar::kCityBorder), 1.0F / 3.0F);
    EXPECT_FLOAT_EQ(f.scalar(t, poly::scalar::kUnitHp), 1.0F);  // full health
    // A plain tile next to the capital: inside its borders (radius 1), nothing on it.
    const int u = tile(s, 3, 3);
    EXPECT_EQ(f.field(u, poly::field::kTerrain), 0);    // Plain
    EXPECT_EQ(f.field(u, poly::field::kResource), 7);   // none
    EXPECT_EQ(f.field(u, poly::field::kBuilding), 19);  // none
    EXPECT_EQ(f.field(u, poly::field::kTerritory), 0);
    EXPECT_EQ(f.field(u, poly::field::kCityOwner), 8);  // none: not the city tile
    EXPECT_EQ(f.field(u, poly::field::kUnitType), 12);  // none
    // Forest with animals (Resource::Animal = 2), outside every border.
    const int w = tile(s, 2, 4);
    EXPECT_EQ(f.field(w, poly::field::kTerrain), 6);  // Forest
    EXPECT_EQ(f.field(w, poly::field::kResource), 2);
    EXPECT_EQ(f.field(w, poly::field::kTerritory), 9);  // none
}

TEST(Features, PerspectiveIsRelativeToTheObserver) {
    const GameState s = two_tribes(poly::ObsMode::Full);
    const Features f0 = features(s, 0);
    const Features f1 = features(s, 1);
    const int a = tile(s, 2, 2);  // tribe 0's capital
    const int b = tile(s, 8, 8);  // tribe 1's capital
    EXPECT_EQ(f0.field(a, poly::field::kCityOwner), 0);
    EXPECT_EQ(f0.field(b, poly::field::kCityOwner), 1);
    EXPECT_EQ(f1.field(a, poly::field::kCityOwner), 1);
    EXPECT_EQ(f1.field(b, poly::field::kCityOwner), 0);
    EXPECT_EQ(f1.field(a, poly::field::kUnitOwner), 1);
    EXPECT_EQ(f1.field(b, poly::field::kTerritory), 0);
    // Population is only shown for the observer's own cities (04 §7 hides others').
    EXPECT_EQ(f0.scalar(b, poly::scalar::kCityPopulation), 0.0F);
    // Global: each sees its own tribe type at r = 0.
    EXPECT_EQ(f0.global[poly::gfeat::kTribeType + 0], 1.0F);  // Xin Xi
    EXPECT_EQ(f1.global[poly::gfeat::kTribeType + 3], 1.0F);  // Imperius
    EXPECT_EQ(f1.global[poly::gfeat::kTribeType + 0], 0.0F);
}

// What is wrong with the tech bits and monument one-hots of `g` for tribe t, or "" if nothing.
std::string global_problem(const poly::TribeState& t,
                           const std::array<float, poly::gfeat::kSize>& g) {
    const auto at = [&g](int k) { return g[static_cast<std::size_t>(k)]; };
    for (int k = 0; k < poly::kNumTechs; ++k) {
        if (at(poly::gfeat::kTechs + k) != (t.has_tech(static_cast<poly::Tech>(k)) ? 1.0F : 0.0F)) {
            return "tech bit " + std::to_string(k);
        }
    }
    for (int m = 0; m < poly::kNumMonuments; ++m) {
        float sum = 0;
        for (int k = 0; k < 4; ++k) {
            sum += at(poly::gfeat::kMonuments + (4 * m) + k);
        }
        if (sum != 1.0F) {
            return "monument " + std::to_string(m) + " is not one-hot";
        }
    }
    return "";
}

TEST(Features, GlobalVector) {
    GameState s = two_tribes(poly::ObsMode::Partial);
    s.tribes[0].stars = 13;
    s.tribes[0].score = 1250;
    s.tribes[1].score = 500;
    s.tick = 10;
    const Features f = features(s, 0);
    const auto& g = f.global;
    EXPECT_FLOAT_EQ(g[poly::gfeat::kStars], 13.0F / 20.0F);
    EXPECT_FLOAT_EQ(g[poly::gfeat::kScore + 0], 1.25F);
    EXPECT_FLOAT_EQ(g[poly::gfeat::kScore + 1], 0.5F);
    EXPECT_FLOAT_EQ(g[poly::gfeat::kScore + 2], 0.0F);  // no third tribe
    EXPECT_FLOAT_EQ(g[poly::gfeat::kTick], 0.2F);
    EXPECT_EQ(g[poly::gfeat::kPartialObs], 1.0F);
    EXPECT_EQ(g[poly::gfeat::kScoreMode], 0.0F);
    EXPECT_FLOAT_EQ(g[poly::gfeat::kNumTribes], 0.25F);
    EXPECT_EQ(global_problem(s.tribes[0], g), "");
}

TEST(Features, OpponentsHiddenValuesDontShow) {
    GameState s = two_tribes(poly::ObsMode::Partial);
    const Features before = features(s, 0);
    s.tribes[1].stars = 99;
    s.tribes[1].techs = (1U << poly::kNumTechs) - 1;
    s.cities[s.tribes[1].capital].population = 3;
    EXPECT_TRUE(same(before, features(s, 0)));
    s.tribes[0].stars = 99;  // positive control: the observer's own stars do show
    EXPECT_FALSE(same(before, features(s, 0)));
}

TEST(Features, ScoresAndVisionAreTheObserversToo) {
    GameState s = two_tribes(poly::ObsMode::Partial);
    s.tribes[0].score = 300;
    s.tribes[1].score = 700;
    const Features f1 = features(s, 1);
    EXPECT_FLOAT_EQ(f1.global[poly::gfeat::kScore + 0], 0.7F);  // r = 0 is tribe 1 itself
    EXPECT_FLOAT_EQ(f1.global[poly::gfeat::kScore + 1], 0.3F);
    // Starting vision covers the capital's surroundings, not the other corner (6 tiles away).
    const auto own = static_cast<std::size_t>(tile(s, 8, 8));
    const auto far = static_cast<std::size_t>(tile(s, 2, 2));
    EXPECT_NE(f1.flags[own] & poly::flag::kVisible, 0);
    EXPECT_EQ(f1.flags[far] & poly::flag::kVisible, 0);
}

TEST(Features, AFoggedTileShowsOnlyFog) {
    const GameState s = two_tribes(poly::ObsMode::Partial);
    const Features f = features(s, 0);
    const int t = tile(s, 8, 8);                        // tribe 1's capital, hidden from tribe 0
    EXPECT_EQ(f.field(t, poly::field::kTerrain), 7);    // Fog
    EXPECT_EQ(f.field(t, poly::field::kTerritory), 9);  // none: a fogged tile's owner says nothing
    EXPECT_EQ(f.field(t, poly::field::kCityOwner), 8);  // none
    EXPECT_EQ(f.field(t, poly::field::kUnitType), 12);  // none
    EXPECT_EQ(f.flags[static_cast<std::size_t>(t)], 0);
    EXPECT_EQ(f.scalar(t, poly::scalar::kCityLevel), 0.0F);
}

TEST(Features, UnitHpIsAFractionOfItsOwnMax) {
    GameState s = two_tribes(poly::ObsMode::Full);
    poly::Unit& u = s.units[s.unit_at[static_cast<std::size_t>(tile(s, 2, 2))]];
    u.max_hp = 15;  // a veteran warrior: max 10 + 5
    u.hp = 12;
    u.veteran = true;
    const Features f = features(s, 0);
    EXPECT_FLOAT_EQ(f.scalar(tile(s, 2, 2), poly::scalar::kUnitHp), 0.8F);
    EXPECT_NE(f.flags[static_cast<std::size_t>(tile(s, 2, 2))] & poly::flag::kVeteran, 0);
}

TEST(Features, NetworkIndexByHand) {
    // Spatial: index (24x + y) * 332 + ch  ->  (ch * side + x) * side + y.
    EXPECT_EQ(poly::network_index((((24 * 1) + 2) * 332) + 5, 11), (((5 * 11) + 1) * 11) + 2);
    EXPECT_EQ(poly::network_index((((24 * 10) + 10) * 332) + 331, 11), (331 * 121) + 120);
    // Global: after the 332 * side * side spatial outputs.
    EXPECT_EQ(poly::network_index(576 * 332, 11), 332 * 121);
    EXPECT_EQ(poly::network_index((576 * 332) + 129, 16), (332 * 256) + 129);
    EXPECT_EQ(poly::network_policy_size(11), (332 * 121) + 130);
}

// What is wrong with network_index at `side` as a map from (on-map tiles x channels) + globals onto
// [0, network_policy_size(side)): out of range, a collision, or an output nothing reaches.
std::string bijection_problem(int side) {
    const int size = poly::network_policy_size(side);
    std::vector<char> hit(static_cast<std::size_t>(size), 0);
    std::vector<int> policies;
    for (int x = 0; x < side; ++x) {
        for (int y = 0; y < side; ++y) {
            for (int ch = 0; ch < poly::kPolicyChannels; ++ch) {
                policies.push_back((((24 * x) + y) * 332) + ch);
            }
        }
    }
    for (int gk = 0; gk < poly::kPolicyGlobal; ++gk) {
        policies.push_back((576 * 332) + gk);
    }
    for (const int policy : policies) {
        const int k = poly::network_index(policy, side);
        if (k < 0 || k >= size || hit[static_cast<std::size_t>(k)] != 0) {
            return "policy " + std::to_string(policy) + " -> " + std::to_string(k);
        }
        hit[static_cast<std::size_t>(k)] = 1;
    }
    return std::ranges::count(hit, 0) == 0 ? "" : "an output no policy index reaches";
}

TEST(Features, NetworkIndexIsABijectionOntoTheOutput) {
    for (const int side : {11, 16, 24}) {
        EXPECT_EQ(bijection_problem(side), "") << "side " << side;
    }
}

TEST(FeaturesDeathTest, NetworkIndexOutsideTheMapFailsLoudly) {
    EXPECT_DEATH((void)poly::network_index(((24 * 11) + 0) * 332, 11), "outside the map");
    EXPECT_DEATH((void)poly::network_index(((24 * 0) + 11) * 332, 11), "outside the map");
    EXPECT_DEATH((void)poly::network_index(-1, 11), "out of range");
}

// ---- property tests over random games ----

std::string random_cell(poly::Rng& rng) {
    static constexpr std::array<const char*, 10> kCells = {".",   ".:f", "f",   "f:a", "m",
                                                           "m:o", "s",   "s:h", "d",   "v"};
    return kCells[rng.below(kCells.size())];
}

GameState random_game_start(std::uint64_t seed, poly::ObsMode obs, poly::Chance& chance) {
    poly::Rng rng(seed);
    std::vector<std::pair<std::pair<int, int>, std::string>> cells;
    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 16; ++y) {
            cells.emplace_back(std::pair{x, y}, random_cell(rng));
        }
    }
    cells.emplace_back(std::pair{2, 2}, "c:0");
    cells.emplace_back(std::pair{13, 13}, "c:3");
    return poly::load_level(level(16, cells), poly::GameMode::Capitals, obs, chance);
}

// The hidden part of s for observer p, scrambled: every tile p can't see (terrain, resource,
// building, the unit on it) and the other tribe's private values. Visible tiles are untouched.
void scramble_hidden(GameState& s, int p, poly::Rng& rng) {
    const poly::TribeState& me = s.tribes[static_cast<std::size_t>(p)];
    for (int t = 0; t < s.side * s.side; ++t) {
        const auto ti = static_cast<std::size_t>(t);
        if (me.visible.test(ti)) {
            continue;
        }
        s.terrain[ti] = static_cast<poly::Terrain>(rng.below(7));
        s.resource[ti] = static_cast<poly::Resource>(rng.below(8));
        s.building[ti] = static_cast<poly::Building>(rng.below(20));
        s.network.flip(ti);
        if (const poly::Slot u = s.unit_at[ti]; u != poly::kNoSlot) {
            s.units[u].hp = static_cast<std::int16_t>(1 + rng.below(9));
            s.units[u].veteran = !s.units[u].veteran;
        }
    }
    for (int q = 0; q < s.num_tribes; ++q) {
        if (q == p) {
            continue;
        }
        poly::TribeState& t = s.tribes[static_cast<std::size_t>(q)];
        t.stars = static_cast<std::int32_t>(rng.below(50));
        t.techs = static_cast<std::uint32_t>(rng.next()) & ((1U << poly::kNumTechs) - 1);
        t.kills += 3;
        for (const poly::Slot c : t.cities) {
            s.cities[c].population = static_cast<std::int16_t>(rng.below(5));
        }
    }
}

struct Counts {
    long long decisions = 0;
    long long hidden_tiles = 0;  // summed over decisions: how much the scramble had to work with
    long long actions = 0;
};

// What is wrong at one decision of player p, or "" if nothing: its features must not change when
// the hidden part of the state is scrambled, and its legal actions must land on distinct outputs.
std::string decision_problem(const GameState& s, int p, const poly::ActionList& legal,
                             poly::Rng& rng, Counts& counts) {
    const poly::Observation o = poly::observe(s, p);
    Features a;
    Features b;
    poly::encode_features(o, a);
    GameState scrambled = s;
    scramble_hidden(scrambled, p, rng);
    poly::encode_features(poly::observe(scrambled, p), b);
    if (!same(a, b)) {
        return "features changed with only hidden state";
    }
    counts.hidden_tiles +=
        (static_cast<long long>(s.side) * s.side) -
        static_cast<long long>(s.tribes[static_cast<std::size_t>(p)].visible.count());
    ++counts.decisions;
    std::set<int> outs;
    for (const poly::Action& act : legal) {
        const int k = poly::network_index(poly::encode_action(o, act), s.side);
        if (!outs.insert(k).second) {
            return "two legal actions share output " + std::to_string(k);
        }
        ++counts.actions;
    }
    return "";
}

// One random game (both tribes play random legal actions); at every decision, the features of the
// player to move are unchanged by scrambling the hidden part, and its legal actions land on
// distinct network outputs.
void check_game(std::uint64_t seed, poly::ObsMode obs, Counts& counts) {
    poly::SampledChance chance(seed);
    poly::Rng rng(seed * 7919U);
    GameState s = random_game_start(seed, obs, chance);
    poly::ActionList legal;
    while (!s.game_over && s.tick < 25 && !::testing::Test::HasFatalFailure()) {
        const int before = s.tick;
        const int p = poly::next_turn_tribe(s);
        if (s.tick != before && poly::update_game_over(s)) {
            break;
        }
        poly::start_turn(s, p);
        for (int step = 0; step < 40 && !s.game_over; ++step) {
            poly::legal_actions(s, legal);
            ASSERT_EQ(decision_problem(s, p, legal, rng, counts), "")
                << "seed " << seed << " tick " << s.tick;
            const poly::Action act = legal[static_cast<int>(rng.below(legal.size()))];
            if (act.type == poly::ActionType::EndTurn) {
                break;
            }
            poly::apply_action(s, act, chance);
            poly::update_game_over(s);
        }
        poly::end_turn(s);
        poly::update_game_over(s);
    }
}

TEST(Features, HiddenStateNeverChangesFeaturesPartialObs) {
    Counts counts;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        check_game(seed, poly::ObsMode::Partial, counts);
    }
    EXPECT_GT(counts.decisions, 1000);
    EXPECT_GT(counts.hidden_tiles, 100 * counts.decisions);  // the scramble had real fog to work on
    EXPECT_GT(counts.actions, 10000);
}

TEST(Features, LegalActionsMapToDistinctOutputsFullObs) {
    Counts counts;
    for (std::uint64_t seed = 11; seed <= 14; ++seed) {
        check_game(seed, poly::ObsMode::Full, counts);
    }
    EXPECT_GT(counts.actions, 10000);
}

TEST(Features, ScramblingAVisibleTileDoesChangeFeatures) {
    // Positive control for the property test: the same comparison catches a visible change.
    GameState s = two_tribes(poly::ObsMode::Partial);
    const Features before = features(s, 0);
    s.terrain[static_cast<std::size_t>(tile(s, 3, 3))] = poly::Terrain::Mountain;  // visible
    EXPECT_FALSE(same(before, features(s, 0)));
}

}  // namespace
