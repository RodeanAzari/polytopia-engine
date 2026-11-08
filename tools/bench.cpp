// Simulator benchmark (`make bench`, docs/buildplan.md §6 Speed): uniform-random playouts on levels
// written by the Tribes side (oracle/bridge/core/game/OracleBench.java), timed with the same loop:
// rule facts 04 §2 on the true state, action generation after every executed action, no state
// copies, level loading not timed. Single thread.
//
// Output, one CSV row per game on stdout (same columns as OracleBench):
//   side,players,mode,levelSeed,ticks,steps,turns,seconds
//
// Usage: bench <level-file>...
// (file name level-<players>-<mode>-<seed>.txt; first line is the mode)

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"

namespace {

struct Result {
    int ticks = 0;
    long long steps = 0;  // agent decisions, End turn included
    long long turns = 0;
    double seconds = 0;
};

Result play(poly::GameState s, std::uint64_t seed) {
    poly::SampledChance chance(seed);
    poly::Rng agent(seed ^ 0x5DEECE66DULL);
    poly::ActionList legal;
    Result r;
    const auto t0 = std::chrono::steady_clock::now();
    while (!s.game_over) {
        const int before = s.tick;
        const int p = poly::next_turn_tribe(s);
        if (s.tick != before && poly::update_game_over(s)) {  // Tribes checks before each tick
            break;
        }
        ++r.turns;
        poly::start_turn(s, p);
        poly::legal_actions(s, legal);
        bool ask = true;  // Tribes' continueTurn: decided before the chosen action runs
        while (ask) {
            const poly::Action a =
                legal[static_cast<int>(agent.below(static_cast<std::uint32_t>(legal.size())))];
            ++r.steps;
            ask = legal.size() != 1 || legal[0].type != poly::ActionType::EndTurn;
            if (a.type == poly::ActionType::EndTurn) {
                break;
            }
            poly::apply_action(s, a, chance);
            poly::legal_actions(s, legal);
            if (poly::update_game_over(s)) {
                break;
            }
        }
        poly::end_turn(s);
        poly::update_game_over(s);
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.ticks = s.tick;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: bench <level-file>...\n";
        return 2;
    }
    const std::vector<std::string> paths(
        argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    try {
        for (const std::string& path : paths) {
            std::ifstream in(path);
            std::string mode;
            if (!in || !std::getline(in, mode) || (mode != "Capitals" && mode != "Score")) {
                throw std::runtime_error("cannot read " + path);
            }
            std::vector<std::string> lines;
            for (std::string line; std::getline(in, line);) {
                lines.push_back(line);
            }
            // level-<players>-<mode>-<seed>.txt
            const std::string name = path.substr(path.rfind('/') + 1);
            const auto dash = name.rfind('-');
            const std::uint64_t seed = std::stoull(name.substr(dash + 1));
            const int players = std::stoi(name.substr(6, name.find('-', 6) - 6));
            poly::SampledChance load_chance(seed);  // full obs never draws at load
            const poly::GameState s = poly::load_level(
                lines, mode == "Capitals" ? poly::GameMode::Capitals : poly::GameMode::Score,
                poly::ObsMode::Full, load_chance);
            const Result r = play(s, seed);
            std::ostringstream row;
            row << "sim," << players << ',' << mode << ',' << seed << ',' << r.ticks << ','
                << r.steps << ',' << r.turns << ',' << std::fixed << std::setprecision(6)
                << r.seconds << '\n';
            std::cout << row.str();
        }
    } catch (const std::exception& e) {
        std::cerr << "bench: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
