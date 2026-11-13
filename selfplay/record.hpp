#pragma once

// Training records (ADR-012 §2): one file per game, written by C++ and read by Python
// (nn/records.py); Python never re-derives features (ADR-011 §5). Little-endian, packed, in this
// order:
//
//   header   char[8] "PLYREC1\0"; u32 format version (kRecordVersion); u32 features version
//            (kFeaturesVersion); u32 side; u32 tribes; u64 level seed; u64 game seed;
//            u8 obs (0 full, 1 partial); u8 mode (0 Capitals, 1 Score); i8 winner (-1 none);
//            u8 draw; u32 ticks; u32 decisions (records below); u32 agent-spec length; the spec
//   record   u8 to_move; u8 searched (always 1 for now); u16 edges;
//            fields  u8[side * side * 9]   (features.hpp, tile = x * side + y)
//            flags   u8[side * side]
//            scalars f32[side * side * 4]
//            global  f32[80]
//            edges x (u32 network index at this side; u32 visits), the search's root edges
//
// The value target is the result from to_move's view (draw 0, win +1, loss -1), derived by the
// reader from the header. Records are only the decisions the agent searched (forced moves with one
// candidate are skipped: they carry no policy information).

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "poly/features.hpp"

namespace poly::selfplay {

inline constexpr std::uint32_t kRecordVersion = 1;

struct Decision {
    std::uint8_t to_move = 0;
    std::vector<std::uint8_t> fields;                            // side * side * field::kCount
    std::vector<std::uint8_t> flags;                             // side * side
    std::vector<float> scalars;                                  // side * side * scalar::kCount
    std::vector<float> global;                                   // gfeat::kSize
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;  // (network index, visits)
};

struct GameRecord {
    std::uint32_t features_version = kFeaturesVersion;
    std::uint32_t side = 0;
    std::uint32_t tribes = 0;
    std::uint64_t level_seed = 0;
    std::uint64_t game_seed = 0;
    std::uint8_t obs = 0;
    std::uint8_t mode = 0;
    std::int8_t winner = -1;
    std::uint8_t draw = 0;
    std::uint32_t ticks = 0;
    std::string agent;
    std::vector<Decision> decisions;
};

// The compact features of one decision (a copy of the map-sized part of `f`).
Decision make_decision(const Features& f, int to_move,
                       std::vector<std::pair<std::uint32_t, std::uint32_t>> edges);

// Writes `path` atomically (a temporary file, then a rename), so a complete file is a finished
// game: an interrupted run leaves no partial record. Throws on I/O errors.
void write_record(const std::filesystem::path& path, const GameRecord& g);

// Reads a file written by write_record. Throws on a bad magic, version or truncation.
GameRecord read_record(const std::filesystem::path& path);

}  // namespace poly::selfplay
