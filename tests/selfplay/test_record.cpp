// Training records (selfplay/record.hpp) and the recording agent. Byte offsets and sizes below are
// computed by hand from the layout in record.hpp's header comment, not from the writer:
//   header = 56 + agent length (magic 8, version 4, features version 4, side 4, tribes 4, level
//            seed 8, game seed 8, obs 1, mode 1, winner 1, draw 1, ticks 4, decisions 4, length 4)
//   record = 4 + tiles * 9 + tiles + tiles * 16 + 80 * 4 + edges * 8

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "poly/action_encoding.hpp"
#include "poly/chance.hpp"
#include "poly/features.hpp"
#include "poly/level.hpp"
#include "poly/observation.hpp"
#include "poly/rules.hpp"
#include "selfplay/record.hpp"
#include "selfplay/recorder.hpp"

namespace {

using poly::selfplay::Decision;
using poly::selfplay::GameRecord;

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::path(::testing::TempDir()) / name;
}

std::vector<char> bytes_of(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

template <typename T>
T at(const std::vector<char>& b, std::size_t offset) {
    T v{};
    std::memcpy(&v, &b.at(offset), sizeof(T));
    return v;
}

// A side-11 record with one decision whose every value is distinct enough to catch a swap.
GameRecord sample() {
    constexpr std::size_t kTiles = 121;
    Decision d{.to_move = 1,
               .fields = std::vector<std::uint8_t>(kTiles * std::size_t{9}),
               .flags = std::vector<std::uint8_t>(kTiles),
               .scalars = std::vector<float>(kTiles * std::size_t{4}),
               .global = std::vector<float>(80),
               .edges = {{40172, 7}, {5, 0}, {12345, 993}}};
    for (std::size_t i = 0; i < d.fields.size(); ++i) {
        d.fields[i] = static_cast<std::uint8_t>(i % 13);
    }
    for (std::size_t i = 0; i < d.flags.size(); ++i) {
        d.flags[i] = static_cast<std::uint8_t>(i % 32);
    }
    for (std::size_t i = 0; i < d.scalars.size(); ++i) {
        d.scalars[i] = static_cast<float>(i) * 0.25F;
    }
    for (std::size_t i = 0; i < d.global.size(); ++i) {
        d.global[i] = -static_cast<float>(i);
    }
    return {.side = 11,
            .tribes = 2,
            .level_seed = 1709954522181ULL,
            .game_seed = 0xFEDCBA9876543210ULL,
            .obs = 1,
            .mode = 0,
            .winner = 1,
            .draw = 0,
            .ticks = 37,
            .agent = "PUCT",
            .decisions = {d}};
}

bool same(const Decision& a, const Decision& b) {
    return a.to_move == b.to_move && a.fields == b.fields && a.flags == b.flags &&
           a.scalars == b.scalars && a.global == b.global && a.edges == b.edges;
}

// The integer fields at their hand-computed offsets, in one list (header, then the decision).
std::vector<std::uint64_t> integers(const std::vector<char>& b) {
    constexpr std::size_t kR = 60;  // first decision: header 56 + "PUCT"
    constexpr std::size_t kE = kR + 4 + 1089 + 121 + 1936 + 320;  // its edges
    return {at<std::uint32_t>(b, 8),       at<std::uint32_t>(b, 12),
            at<std::uint32_t>(b, 16),      at<std::uint32_t>(b, 20),
            at<std::uint64_t>(b, 24),      at<std::uint64_t>(b, 32),
            at<std::uint8_t>(b, 40),       at<std::uint8_t>(b, 42),
            at<std::uint32_t>(b, 44),      at<std::uint32_t>(b, 48),
            at<std::uint32_t>(b, 52),      at<std::uint8_t>(b, kR),
            at<std::uint16_t>(b, kR + 2),  at<std::uint8_t>(b, kR + 4 + 14),
            at<std::uint32_t>(b, kE),      at<std::uint32_t>(b, kE + 4),
            at<std::uint32_t>(b, kE + 16), at<std::uint32_t>(b, kE + 20)};
}

TEST(Record, LayoutMatchesTheSpec) {
    const auto path = temp_file("layout.rec");
    poly::selfplay::write_record(path, sample());
    const std::vector<char> b = bytes_of(path);
    ASSERT_EQ(b.size(), (56U + 4U) + (4U + 1089U + 121U + 1936U + 320U + 24U));
    EXPECT_EQ(std::string(b.data(), 7), "PLYREC1");
    // versions 1, 1; side 11; 2 tribes; seeds; partial; winner 1; 37 ticks; 1 decision; spec
    // length 4; to_move 1; 3 edges; fields[14] = 14 % 13; the first and third edges
    const std::vector<std::uint64_t> expected = {
        1,     1, 11,    2,  1709954522181ULL, 0xFEDCBA9876543210ULL, 1, 1, 37, 1, 4, 1, 3, 1,
        40172, 7, 12345, 993};
    EXPECT_EQ(integers(b), expected);
    EXPECT_EQ(std::string(&b[56], 4), "PUCT");
    EXPECT_EQ(at<float>(b, 60 + 4 + 1089 + 121 + (4 * 3)), 0.75F);         // scalars[3]
    EXPECT_EQ(at<float>(b, 60 + 4 + 1089 + 121 + 1936 + (4 * 2)), -2.0F);  // global[2]
}

TEST(Record, RoundTrip) {
    const auto path = temp_file("roundtrip.rec");
    const GameRecord g = sample();
    poly::selfplay::write_record(path, g);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));  // renamed into place
    const GameRecord r = poly::selfplay::read_record(path);
    EXPECT_EQ(r.side, g.side);
    EXPECT_EQ(r.level_seed, g.level_seed);
    EXPECT_EQ(r.game_seed, g.game_seed);
    EXPECT_EQ(r.obs, g.obs);
    EXPECT_EQ(r.winner, g.winner);
    EXPECT_EQ(r.ticks, g.ticks);
    EXPECT_EQ(r.agent, g.agent);
    ASSERT_EQ(r.decisions.size(), 1U);
    EXPECT_TRUE(same(r.decisions[0], g.decisions[0]));
}

// True if read_record refuses a file holding `b`.
bool rejected(const std::filesystem::path& path, const std::vector<char>& b) {
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(b.data(), static_cast<std::streamsize>(b.size()));
    try {
        (void)poly::selfplay::read_record(path);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

TEST(Record, DamagedFilesAreRejected) {
    const auto path = temp_file("damaged.rec");
    poly::selfplay::write_record(path, sample());
    const std::vector<char> good = bytes_of(path);
    EXPECT_FALSE(rejected(path, good));                                            // control
    EXPECT_TRUE(rejected(path, std::vector<char>(good.begin(), good.end() - 1)));  // truncated
    std::vector<char> trailing = good;
    trailing.push_back(0);
    EXPECT_TRUE(rejected(path, trailing));
    std::vector<char> magic = good;
    magic[0] = 'X';
    EXPECT_TRUE(rejected(path, magic));
    std::vector<char> version = good;
    version[8] = 9;
    EXPECT_TRUE(rejected(path, version));
}

TEST(Record, ADecisionOfTheWrongSideIsRefused) {
    GameRecord g = sample();
    g.side = 12;
    EXPECT_THROW(poly::selfplay::write_record(temp_file("wrong.rec"), g), std::runtime_error);
}

// ---- the recording agent, on one real decision ----

// Two tribes on an 11 x 11 plain map, tribe 0 to move (capitals at (2, 2) and (8, 8)).
poly::GameState start() {
    std::vector<std::string> lines(11, ".,.,.,.,.,.,.,.,.,.,.");
    lines[2] = ".,.,c:0,.,.,.,.,.,.,.,.";
    lines[8] = ".,.,.,.,.,.,.,.,c:3,.,.";
    poly::SampledChance chance(1);
    poly::GameState s =
        poly::load_level(lines, poly::GameMode::Capitals, poly::ObsMode::Partial, chance);
    poly::start_turn(s, 0);
    return s;
}

// What is wrong with a recorded root, or "" if nothing: every edge is a legal action's output,
// none twice, the visits are real, and `chosen` is a most-visited edge (ADR-008 §5).
std::string root_problem(const poly::Observation& o, const poly::ActionList& legal,
                         const Decision& d, const poly::Action& chosen) {
    const int side = o.view().side;
    const auto out = [&](const poly::Action& a) {
        return static_cast<std::uint32_t>(poly::network_index(poly::encode_action(o, a), side));
    };
    std::set<std::uint32_t> legal_outs;
    for (const poly::Action& a : legal) {
        legal_outs.insert(out(a));
    }
    std::set<std::uint32_t> seen;
    std::uint32_t total = 0;
    std::uint32_t best = 0;
    for (const auto& [index, visits] : d.edges) {
        if (!legal_outs.contains(index) || !seen.insert(index).second) {
            return "edge " + std::to_string(index) + " is not a distinct legal output";
        }
        total += visits;
        best = std::max(best, visits);
    }
    const auto it =
        std::ranges::find(d.edges, out(chosen), &std::pair<std::uint32_t, std::uint32_t>::first);
    if (total == 0 || it == d.edges.end() || it->second != best) {
        return "no visits, or the chosen action is not a most-visited edge";
    }
    return "";
}

TEST(Recorder, RecordsTheSearchedRootOverTheNetworksOutputs) {
    const poly::GameState s = start();
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    ASSERT_GT(legal.size(), 1);
    const poly::Observation o = poly::observe(s, 0);
    poly::selfplay::RecordingPuct agent(42, poly::search::PuctParams{});
    const poly::Action chosen = agent.act(o, std::span(legal.begin(), legal.end()));
    ASSERT_EQ(agent.decisions().size(), 1U);
    const Decision& d = agent.decisions()[0];
    EXPECT_EQ(d.to_move, 0);
    EXPECT_EQ(root_problem(o, legal, d, chosen), "");
    // The features are the observation's, map-sized.
    poly::Features f;
    poly::encode_features(o, f);
    const auto fields = std::span(f.fields).first(std::size_t{121} * 9);
    EXPECT_EQ(d.fields, std::vector<std::uint8_t>(fields.begin(), fields.end()));
    EXPECT_EQ(d.global, std::vector<float>(f.global.begin(), f.global.end()));
}

TEST(Recorder, EdgesAreExactlyThePlainAgentsRootStats) {
    // The plain Phase 3 agent with the same seed searches identically (deterministic); the record
    // must hold its root actions' outputs with its summed visits, edge for edge.
    const poly::GameState s = start();
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    const poly::Observation o = poly::observe(s, 0);
    poly::search::PuctAgent plain(42, poly::search::PuctParams{});
    poly::selfplay::RecordingPuct recording(42, poly::search::PuctParams{});
    (void)plain.act(o, std::span(legal.begin(), legal.end()));
    (void)recording.act(o, std::span(legal.begin(), legal.end()));
    std::vector<std::pair<std::uint32_t, std::uint32_t>> expected;
    for (std::size_t i = 0; i < plain.root_actions().size(); ++i) {
        expected.emplace_back(static_cast<std::uint32_t>(poly::network_index(
                                  poly::encode_action(o, plain.root_actions()[i]), s.side)),
                              static_cast<std::uint32_t>(plain.summed()[i].visits));
    }
    ASSERT_EQ(recording.decisions().size(), 1U);
    EXPECT_EQ(recording.decisions()[0].edges, expected);
}

TEST(Recorder, SameSeedSameRecord) {
    const poly::GameState s = start();
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    const poly::Observation o = poly::observe(s, 0);
    poly::selfplay::RecordingPuct a(7, poly::search::PuctParams{});
    poly::selfplay::RecordingPuct b(7, poly::search::PuctParams{});
    (void)a.act(o, std::span(legal.begin(), legal.end()));
    (void)b.act(o, std::span(legal.begin(), legal.end()));
    ASSERT_EQ(a.decisions().size(), 1U);
    ASSERT_EQ(b.decisions().size(), 1U);
    EXPECT_TRUE(same(a.decisions()[0], b.decisions()[0]));
}

TEST(Recorder, AForcedMoveIsNotRecorded) {
    const poly::GameState s = start();
    poly::ActionList legal;
    poly::legal_actions(s, legal);
    const auto* const end_turn =
        std::ranges::find(legal, poly::ActionType::EndTurn, &poly::Action::type);
    ASSERT_NE(end_turn, legal.end());
    const poly::Observation o = poly::observe(s, 0);
    poly::selfplay::RecordingPuct agent(1, poly::search::PuctParams{});
    (void)agent.act(o, std::span(end_turn, 1));
    EXPECT_TRUE(agent.decisions().empty());
}

}  // namespace
