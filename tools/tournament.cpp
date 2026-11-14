// Round-robin tournament of our agents on our simulator (ADR-007): every pair of agents plays every
// level REPS times in each seating (seat-swapped pairs share a game seed, §1). One CSV row per
// game, appended as games finish; rerunning the same command skips the games already in the CSV
// (§7). The summary (W/D/L, Wilson intervals) is tools/tournament_summary.py.
//
// Usage: tournament --levels=<dir> --agents=Random,RuleBased,OSLA,MC,MCTS,RHEA,PUCT[:k=v:..]
// --reps=<R>
// --out=<csv>
//                   [--id=<tournament id>] [--obs=full|partial] [--jobs=<N>]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "agents.hpp"
#include "poly/types.hpp"
#include "random_game.hpp"
#include "search/match.hpp"

namespace {

std::uint64_t hash_string(const std::string& s) {  // FNV-1a
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : s) {
        h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
    }
    return h;
}

struct Game {
    int index = 0;
    std::string level_path;
    std::uint64_t level_seed = 0;
    int rep = 0;
    std::string agent0;  // plays tribe id 0
    std::string agent1;
};

std::string opt(const std::vector<std::string>& args, const std::string& name,
                const std::string& def) {
    for (const std::string& a : args) {
        if (a.starts_with("--" + name + "=")) {
            return a.substr(name.size() + 3);
        }
    }
    return def;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, ',');) {
        out.push_back(item);
    }
    return out;
}

struct Options {
    std::string levels;
    std::string out;
    std::vector<std::string> agents;
    int reps = 1;
    std::string id;
    bool partial = false;
    int jobs = 4;
};

// Every pair of agents, every level, every rep, both seatings back to back (ADR-007 §1).
std::vector<Game> schedule(const Options& o) {
    std::vector<std::string> paths;
    for (const auto& e : std::filesystem::directory_iterator(o.levels)) {
        paths.push_back(e.path().string());
    }
    std::ranges::sort(paths);
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 0; i < o.agents.size(); ++i) {
        for (std::size_t j = i + 1; j < o.agents.size(); ++j) {
            pairs.emplace_back(o.agents[i], o.agents[j]);
        }
    }
    std::vector<Game> games;
    for (const auto& [a, b] : pairs) {
        for (const std::string& path : paths) {
            const std::string name = std::filesystem::path(path).stem().string();
            const std::uint64_t level_seed = std::stoull(name.substr(name.rfind('-') + 1));
            for (int r = 0; r < o.reps; ++r) {
                for (const bool swapped : {false, true}) {
                    games.push_back({.index = static_cast<int>(games.size()),
                                     .level_path = path,
                                     .level_seed = level_seed,
                                     .rep = r,
                                     .agent0 = swapped ? b : a,
                                     .agent1 = swapped ? a : b});
                }
            }
        }
    }
    return games;
}

// Game indices already in the CSV (resume, ADR-007 §7).
std::set<int> finished(const std::string& csv) {
    std::set<int> done;
    std::ifstream in(csv);
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        done.insert(std::stoi(line.substr(0, line.find(','))));
    }
    return done;
}

const char* result_of_tribe0(const poly::search::MatchResult& r) {
    if (r.draw) {
        return "DRAW";
    }
    return r.winner == 0 ? "WIN" : "LOSS";
}

// Plays game g and returns its CSV row.
std::string play(const Options& o, const Game& g) {
    const std::uint64_t game_seed = polytools::mix(polytools::mix(hash_string(o.id), g.level_seed),
                                                   static_cast<std::uint64_t>(g.rep));
    auto level = polytools::load_level_file(
        g.level_path, o.partial ? poly::ObsMode::Partial : poly::ObsMode::Full);
    const int side = level.state.side;
    const auto a0 = polytools::make_agent(g.agent0, polytools::agent_seed(game_seed, 0), side);
    const auto a1 = polytools::make_agent(g.agent1, polytools::agent_seed(game_seed, 1), side);
    const std::array<poly::search::Agent*, 2> seats = {a0.get(), a1.get()};
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = poly::search::play_match(level.state, seats, game_seed);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const auto tribe = [&](std::size_t i) {
        return poly::kTribeName[static_cast<std::size_t>(level.state.tribes[i].type)];
    };
    std::ostringstream row;
    row << g.index << ',' << o.id << ',' << g.level_seed << ',' << g.rep << ',' << game_seed << ','
        << (o.partial ? "partial" : "full") << ',' << g.agent0 << ',' << g.agent1 << ',' << tribe(0)
        << ',' << tribe(1) << ',' << result_of_tribe0(r) << ',' << r.score[0] << ',' << r.score[1]
        << ',' << r.ticks << ',' << r.decisions << ',' << r.illegal << ',' << secs << '\n';
    return row.str();
}

int run(const Options& o) {
    const std::vector<Game> games = schedule(o);
    const bool exists = std::filesystem::exists(o.out);
    const std::set<int> done = exists ? finished(o.out) : std::set<int>{};
    std::ofstream csv(o.out, std::ios::app);
    if (!exists) {
        csv << "game,tournament,levelSeed,rep,gameSeed,obs,agent0,agent1,tribe0,tribe1,result0,"
               "score0,score1,ticks,decisions,illegal,seconds\n";
    }
    std::mutex mu;
    std::atomic<std::size_t> next{0};
    std::atomic<int> played{0};
    const auto worker = [&] {
        for (std::size_t k = next++; k < games.size(); k = next++) {
            if (done.contains(games[k].index)) {
                continue;
            }
            const std::string row = play(o, games[k]);
            const std::scoped_lock lock(mu);
            csv << row << std::flush;
            ++played;
        }
    };
    {
        std::vector<std::jthread> pool;
        pool.reserve(static_cast<std::size_t>(o.jobs));
        for (int t = 0; t < o.jobs; ++t) {
            pool.emplace_back(worker);
        }
    }  // joins
    std::cout << played << " games played, " << done.size() << " already in " << o.out << " ("
              << games.size() << " scheduled)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);  // NOLINT(*-pointer-arithmetic)
        Options o;
        o.levels = opt(args, "levels", "");
        o.out = opt(args, "out", "");
        if (o.levels.empty() || o.out.empty()) {
            std::cerr << "usage: tournament --levels=<dir> --agents=A,B,... --reps=<R> --out=<csv> "
                         "[--id=<id>] [--obs=full|partial] [--jobs=<N>]\n";
            return 2;
        }
        o.agents = split(opt(args, "agents", "Random,RuleBased"));
        o.reps = std::stoi(opt(args, "reps", "1"));
        o.id = opt(args, "id", std::filesystem::path(o.out).stem().string());
        o.partial = opt(args, "obs", "full") == "partial";
        o.jobs = std::stoi(opt(args, "jobs", "4"));
        for (const std::string& a : o.agents) {
            polytools::check_agent(a);  // unknown names fail before any game
        }
        return run(o);
    } catch (const std::exception& e) {
        std::cerr << "tournament: " << e.what() << '\n';
        return 1;
    }
}
