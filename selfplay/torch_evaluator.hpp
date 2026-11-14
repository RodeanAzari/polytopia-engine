#pragma once

// The network as a PUCT leaf evaluator (ADR-012 §1): a TorchScript module exported by nn/export.py,
// run by LibTorch on the GPU in batches across threads. Each search thread calls evaluate(), which
// queues its request and blocks; one batcher thread takes up to `max_batch` queued requests, runs
// the network once and wakes them. It runs a batch as soon as min(max_batch, searching threads)
// requests wait (threads inside begin_search/end_search), or after `max_wait_us` with fewer.
// Built only with POLY_TORCH (CMake), so CI and the plain build need no LibTorch.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "poly/features.hpp"
#include "search/leaf_eval.hpp"

namespace poly::selfplay {

struct EvaluatorStats {
    long long batches = 0;
    long long positions = 0;
    long long graph_batches = 0;  // of `batches`, those replayed from a CUDA graph
    double gpu_seconds = 0.0;     // forward passes incl. transfers, measured by the batcher
};

class TorchEvaluator final : public search::LeafEvaluator {
   public:
    // `model`: a TorchScript file from nn/export.py for maps of this `side`. `cuda`: false runs on
    // the CPU (tests). `cuda_graphs`: capture the module as CUDA graphs for batch sizes 8, 16, 32,
    // ... and max_batch, and replay those (padded) instead of plain calls. Throws if the file
    // can't be loaded.
    TorchEvaluator(const std::string& model, int side, int max_batch, int max_wait_us,
                   bool cuda = true, bool cuda_graphs = true);
    ~TorchEvaluator() override;
    TorchEvaluator(const TorchEvaluator&) = delete;
    TorchEvaluator& operator=(const TorchEvaluator&) = delete;
    TorchEvaluator(TorchEvaluator&&) = delete;
    TorchEvaluator& operator=(TorchEvaluator&&) = delete;

    void evaluate(const Observation& obs, std::span<const Action> actions,
                  std::vector<double>& priors, double& value) override;
    void begin_search() override;
    void end_search() override;

    // Raw outputs for one position (tests and parity checks): the edge logits and the value.
    void raw(const Features& f, std::span<const std::int64_t> edges, std::vector<float>& logits,
             float& value);

    [[nodiscard]] EvaluatorStats stats() const;

   private:
    struct Request;
    void run();
    bool forward(std::vector<Request*>& batch);  // true if it replayed a CUDA graph

    struct Impl;  // LibTorch types stay out of this header
    std::unique_ptr<Impl> impl_;
    int side_;
    int max_batch_;
    int max_wait_us_;
    mutable std::mutex mu_;
    std::condition_variable queued_;  // the batcher waits for requests
    std::condition_variable done_;    // callers wait for their results
    std::deque<Request*> queue_;
    int searching_ = 0;  // threads between begin_search and end_search
    bool stop_ = false;
    EvaluatorStats stats_;
    std::thread batcher_;
};

}  // namespace poly::selfplay
