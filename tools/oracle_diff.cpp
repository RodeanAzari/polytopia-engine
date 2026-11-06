// Differential test against oracle logs (oracle/bridge): loads the logged level into our simulator
// and compares our state dump with the oracle's. Stage 1: the initial state after map load (05 §1).
//
// Usage: oracle_diff <log>...   Exit code 0 iff every log matches.

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "poly/dump.hpp"
#include "poly/level.hpp"

namespace {

struct LogStart {
    poly::GameMode mode = poly::GameMode::Capitals;
    poly::ObsMode obs = poly::ObsMode::Full;
    std::vector<std::string> level;
    std::string first_state;  // "STATE ..." through "END"
};

LogStart read_log_start(std::istream& in) {
    LogStart r;
    std::string line;
    bool have_game = false;
    while (std::getline(in, line)) {
        if (line.starts_with("GAME ")) {
            std::istringstream ss(line.substr(5));
            std::string level_seed;
            std::string game_seed;
            std::string mode;
            int full = 1;
            ss >> level_seed >> game_seed >> mode >> full;
            r.mode = mode == "SCORE" ? poly::GameMode::Score : poly::GameMode::Capitals;
            r.obs = full != 0 ? poly::ObsMode::Full : poly::ObsMode::Partial;
            have_game = true;
        } else if (line.starts_with("LEVEL ")) {
            const int n = std::stoi(line.substr(6));
            for (int i = 0; i < n && std::getline(in, line); ++i) {
                r.level.push_back(line);
            }
        } else if (line.starts_with("STATE ")) {
            r.first_state = line;
            while (line != "END" && std::getline(in, line)) {
                r.first_state += '\n' + line;
            }
            break;
        }
    }
    if (!have_game || r.level.empty() || r.first_state.empty()) {
        throw std::runtime_error("log is missing GAME, LEVEL or STATE (logged with --no-states?)");
    }
    return r;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) {
        out.push_back(l);
    }
    return out;
}

// Reports the first differing line and the first differing space-separated field in it.
void report_diff(const std::string& expected, const std::string& actual) {
    const auto e = split_lines(expected);
    const auto a = split_lines(actual);
    for (std::size_t i = 0; i < std::max(e.size(), a.size()); ++i) {
        const std::string el = i < e.size() ? e[i] : "<missing>";
        const std::string al = i < a.size() ? a[i] : "<missing>";
        if (el == al) {
            continue;
        }
        std::istringstream es(el);
        std::istringstream as(al);
        std::string et;
        std::string at;
        int field = 0;
        while (true) {
            const bool eo = static_cast<bool>(es >> et);
            const bool ao = static_cast<bool>(as >> at);
            if (!eo && !ao) {
                break;
            }
            if (!eo || !ao || et != at) {
                std::cerr << "  line " << i << " field " << field << ": oracle '"
                          << (eo ? et : "<end>") << "' ours '" << (ao ? at : "<end>")
                          << "'\n  oracle: " << el.substr(0, 160)
                          << "\n  ours:   " << al.substr(0, 160) << '\n';
                return;
            }
            ++field;
        }
        return;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: oracle_diff <log>...\n";
        return 2;
    }
    const std::vector<std::string> paths(
        argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    int failures = 0;
    for (const std::string& path : paths) {
        try {
            std::ifstream in(path);
            if (!in) {
                throw std::runtime_error("cannot open");
            }
            const LogStart log = read_log_start(in);
            const poly::GameState s = poly::load_level(log.level, log.mode, log.obs);
            const std::string ours = poly::dump_state(s);
            if (ours != log.first_state) {
                ++failures;
                std::cerr << "MISMATCH " << path << " (initial state)\n";
                report_diff(log.first_state, ours);
            }
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "ERROR " << path << ": " << e.what() << '\n';
        }
    }
    std::cout << paths.size() << " logs, " << failures << " failures (initial state)\n";
    return failures == 0 ? 0 : 1;
}
