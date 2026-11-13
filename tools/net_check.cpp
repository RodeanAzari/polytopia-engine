// Parity check for the C++ evaluator (ADR-011 §5, ADR-012 §1): evaluates the first decisions of a
// record file through TorchEvaluator, with the record's own root edges, from `threads` threads at
// once (so batching across threads is exercised), and prints one line per decision:
//   <decision> <value> <logit of edge 0> <logit of edge 1> ...
// tests/python/test_torch_parity.py compares these with the exported module run by PyTorch.
//
// Usage: net_check <model.ts> <record.rec> <decisions> [threads] [max_batch] [graphs 0|1]

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "poly/features.hpp"
#include "selfplay/record.hpp"
#include "selfplay/torch_evaluator.hpp"

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);  // NOLINT(*-pointer-arithmetic)
        if (args.size() < 3) {
            std::cerr << "usage: net_check <model.ts> <record.rec> <decisions> [threads] [batch]\n";
            return 2;
        }
        const auto rec = poly::selfplay::read_record(args[1]);
        const auto n = std::min<std::size_t>(std::stoul(args[2]), rec.decisions.size());
        const int threads = args.size() > 3 ? std::stoi(args[3]) : 1;
        const int batch = args.size() > 4 ? std::stoi(args[4]) : 64;
        const bool graphs = args.size() <= 5 || args[5] != "0";
        const int side = static_cast<int>(rec.side);
        poly::selfplay::TorchEvaluator eval(args[0], side, batch, 2000, true, graphs);

        std::vector<std::vector<float>> logits(n);
        std::vector<float> values(n);
        const auto work = [&](int t) {
            poly::Features f;
            std::vector<std::int64_t> edges;
            for (auto i = static_cast<std::size_t>(t); i < n;
                 i += static_cast<std::size_t>(threads)) {
                const auto& d = rec.decisions[i];
                f.side = side;
                std::ranges::copy(d.fields, f.fields.begin());
                std::ranges::copy(d.flags, f.flags.begin());
                std::ranges::copy(d.scalars, f.scalars.begin());
                std::ranges::copy(d.global, f.global.begin());
                edges.clear();
                for (const auto& e : d.edges) {
                    edges.push_back(e.first);
                }
                eval.raw(f, edges, logits[i], values[i]);
            }
        };
        {
            std::vector<std::jthread> pool;
            pool.reserve(static_cast<std::size_t>(threads));
            for (int t = 0; t < threads; ++t) {
                pool.emplace_back(work, t);
            }
        }
        std::cout << std::setprecision(9);
        for (std::size_t i = 0; i < n; ++i) {
            std::cout << i << ' ' << values[i];
            for (const float l : logits[i]) {
                std::cout << ' ' << l;
            }
            std::cout << '\n';
        }
        const auto st = eval.stats();
        std::cerr << "net_check: " << st.positions << " positions in " << st.batches << " batches, "
                  << st.graph_batches << " from CUDA graphs\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "net_check: " << e.what() << '\n';
        return 1;
    }
}
