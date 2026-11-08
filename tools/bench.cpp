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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "random_game.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: bench <level-file>...\n";
        return 2;
    }
    const std::vector<std::string> paths(
        argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    try {
        for (const std::string& path : paths) {
            polytools::LevelFile f = polytools::load_level_file(path);
            const auto t0 = std::chrono::steady_clock::now();
            const auto r =
                polytools::play_random(f.state, f.seed, [](const auto&, auto, const auto&) {});
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::ostringstream row;
            row << "sim," << f.players << ',' << f.mode << ',' << f.seed << ',' << f.state.tick
                << ',' << r.steps << ',' << r.turns << ',' << std::fixed << std::setprecision(6)
                << seconds << '\n';
            std::cout << row.str();
        }
    } catch (const std::exception& e) {
        std::cerr << "bench: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
