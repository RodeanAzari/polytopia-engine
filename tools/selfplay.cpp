// Records games of an agent against itself for training (ADR-011 §4, ADR-012 §2): game g plays
// level g mod L (sorted level files) for the (g div L)-th time, Paper and Fog alternating by game
// (or one --obs for all). Each game is one record file <out>/<g>.rec (selfplay/record.hpp) plus a
// row in <out>/games.csv; rerunning the same command skips games whose record exists (resumable).
//
// With --net=<model.ts> (a build with POLY_TORCH; nn/export.py) the agents search with the network
// (ADR-011 §2), batched across the --jobs game threads (--batch, --wait-us); evaluator statistics
// are printed at the end.
//
// Usage: selfplay --levels=<dir> --games=<N> --out=<dir> [--agent=PUCT[:k=v:..]] [--id=<id>]
//                 [--obs=alternate|full|partial] [--jobs=<N>] [--net=<model.ts>] [--batch=<B>]
//                 [--wait-us=<W>]

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "agents.hpp"
#include "random_game.hpp"
#include "search/match.hpp"
#include "selfplay/recorder.hpp"
#ifdef POLY_TORCH
#include "selfplay/torch_evaluator.hpp"
#endif

namespace {

std::uint64_t hash_string(const std::string& s) {  // FNV-1a, as tools/tournament.cpp
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : s) {
        h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
    }
    return h;
}

std::string opt(const std::vector<std::string>& args, const std::string& name,
                const std::string& def) {
    for (const std::string& a : args) {
        if (a.starts_with("--" + name + "=")) {
            return a.substr(name.size() + 3);
        }
    }
    return def;
}

struct Options {
    std::vector<std::string> levels;  // sorted paths
    int games = 0;
    std::filesystem::path out;
    std::string agent = "PUCT";
    std::string id;
    std::string obs = "alternate";
    int jobs = 4;
    std::string net;  // TorchScript model, or empty: the Phase 3 agent
    int batch = 256;
    int wait_us = 2000;
};

const char* result_of_tribe0(const poly::search::MatchResult& r) {
    if (r.draw) {
        return "DRAW";
    }
    return r.winner == 0 ? "WIN" : "LOSS";
}

bool partial_for(const Options& o, int g) {
    if (o.obs == "alternate") {
        return g % 2 == 1;
    }
    return o.obs == "partial";
}

// Plays game g, writes its record and returns its CSV row.
std::string play(const Options& o, int g, poly::search::LeafEvaluator* net) {
    const auto l = static_cast<std::size_t>(g) % o.levels.size();
    const int rep = g / static_cast<int>(o.levels.size());
    const std::string name = std::filesystem::path(o.levels[l]).stem().string();
    const std::uint64_t level_seed = std::stoull(name.substr(name.rfind('-') + 1));
    const std::uint64_t game_seed = polytools::mix(polytools::mix(hash_string(o.id), level_seed),
                                                   static_cast<std::uint64_t>(rep));
    const bool partial = partial_for(o, g);
    auto level = polytools::load_level_file(o.levels[l],
                                            partial ? poly::ObsMode::Partial : poly::ObsMode::Full);
    const std::string_view spec =
        o.agent.size() > 5 ? std::string_view(o.agent).substr(5) : std::string_view{};
    const auto params = poly::search::parse_puct_params(spec);
    poly::selfplay::RecordingPuct a0(polytools::agent_seed(game_seed, 0), params, net);
    poly::selfplay::RecordingPuct a1(polytools::agent_seed(game_seed, 1), params, net);
    const std::array<poly::search::Agent*, 2> seats = {&a0, &a1};

    const auto t0 = std::chrono::steady_clock::now();
    const auto r = poly::search::play_match(level.state, seats, game_seed);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    poly::selfplay::GameRecord rec{
        .side = static_cast<std::uint32_t>(level.state.side),
        .tribes = static_cast<std::uint32_t>(level.state.num_tribes),
        .level_seed = level_seed,
        .game_seed = game_seed,
        .obs = static_cast<std::uint8_t>(partial ? 1 : 0),
        .mode = static_cast<std::uint8_t>(level.state.mode == poly::GameMode::Score ? 1 : 0),
        .winner = static_cast<std::int8_t>(r.draw ? -1 : r.winner),
        .draw = static_cast<std::uint8_t>(r.draw ? 1 : 0),
        .ticks = static_cast<std::uint32_t>(r.ticks),
        .agent = o.net.empty() ? o.agent : o.agent + "+net=" + o.net,
        .decisions = {}};
    // Seat 0's decisions, then seat 1's, each in play order (training doesn't use the order).
    for (auto* a : {&a0, &a1}) {
        auto& d = a->decisions();
        std::ranges::move(d, std::back_inserter(rec.decisions));
    }
    poly::selfplay::write_record(o.out / (std::to_string(g) + ".rec"), rec);

    std::ostringstream row;
    row << g << ',' << o.id << ',' << level_seed << ',' << rep << ',' << game_seed << ','
        << (partial ? "partial" : "full") << ',' << o.agent << ',' << result_of_tribe0(r) << ','
        << r.ticks << ',' << r.decisions << ',' << rec.decisions.size() << ',' << secs << '\n';
    return row.str();
}

int run(const Options& o, poly::search::LeafEvaluator* net) {
    std::filesystem::create_directories(o.out);
    const auto csv_path = o.out / "games.csv";
    const bool exists = std::filesystem::exists(csv_path);
    std::ofstream csv(csv_path, std::ios::app);
    if (!exists) {
        csv << "game,id,levelSeed,rep,gameSeed,obs,agent,result0,ticks,decisions,recorded,"
               "seconds\n";
    }
    std::mutex mu;
    std::atomic<int> next{0};
    std::atomic<int> played{0};
    std::atomic<int> skipped{0};
    const auto worker = [&] {
        for (int g = next++; g < o.games; g = next++) {
            if (std::filesystem::exists(o.out / (std::to_string(g) + ".rec"))) {
                ++skipped;
                continue;
            }
            const std::string row = play(o, g, net);
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
    std::cout << played << " games recorded, " << skipped << " already in " << o.out.string()
              << " (" << o.games << " scheduled)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);  // NOLINT(*-pointer-arithmetic)
        Options o;
        const std::string levels = opt(args, "levels", "");
        o.out = opt(args, "out", "");
        o.games = std::stoi(opt(args, "games", "0"));
        if (levels.empty() || o.out.empty() || o.games <= 0) {
            std::cerr << "usage: selfplay --levels=<dir> --games=<N> --out=<dir> "
                         "[--agent=PUCT[:k=v:..]] [--id=<id>] [--obs=alternate|full|partial] "
                         "[--jobs=<N>]\n";
            return 2;
        }
        for (const auto& e : std::filesystem::directory_iterator(levels)) {
            o.levels.push_back(e.path().string());
        }
        std::ranges::sort(o.levels);
        if (o.levels.empty()) {
            std::cerr << "selfplay: no levels in " << levels << '\n';
            return 2;
        }
        o.agent = opt(args, "agent", "PUCT");
        if (o.agent != "PUCT" && !o.agent.starts_with("PUCT:")) {
            std::cerr << "selfplay: only PUCT agents record (got " << o.agent << ")\n";
            return 2;
        }
        o.id = opt(args, "id", o.out.filename().string());
        o.obs = opt(args, "obs", "alternate");
        if (o.obs != "alternate" && o.obs != "full" && o.obs != "partial") {
            std::cerr << "selfplay: --obs must be alternate, full or partial\n";
            return 2;
        }
        o.jobs = std::stoi(opt(args, "jobs", "4"));
        (void)poly::search::parse_puct_params(
            o.agent.size() > 5 ? std::string_view(o.agent).substr(5) : std::string_view{});
        o.net = opt(args, "net", "");
        o.batch = std::stoi(opt(args, "batch", "256"));
        o.wait_us = std::stoi(opt(args, "wait-us", "2000"));
        if (o.net.empty()) {
            return run(o, nullptr);
        }
#ifdef POLY_TORCH
        const int side = polytools::load_level_file(o.levels.front()).state.side;
        poly::selfplay::TorchEvaluator eval(o.net, side, o.batch, o.wait_us);
        const auto t0 = std::chrono::steady_clock::now();
        const int rc = run(o, &eval);
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const auto st = eval.stats();
        std::cout << "evaluator: " << st.positions << " positions in " << st.batches
                  << " batches (mean " << (st.batches > 0 ? st.positions / st.batches : 0) << "), "
                  << st.gpu_seconds << " s in forward passes of " << secs << " s wall, "
                  << (secs > 0 ? static_cast<double>(st.positions) / secs : 0.0)
                  << " positions/s\n";
        return rc;
#else
        std::cerr << "selfplay: --net needs a build with POLY_TORCH (make build-nn)\n";
        return 2;
#endif
    } catch (const std::exception& e) {
        std::cerr << "selfplay: " << e.what() << '\n';
        return 1;
    }
}
