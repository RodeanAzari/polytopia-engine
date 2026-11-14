#include "selfplay/torch_evaluator.hpp"

#include <ATen/cuda/CUDAGraph.h>
#include <c10/cuda/CUDAGuard.h>
#include <c10/cuda/CUDAStream.h>
#include <torch/csrc/jit/passes/tensorexpr_fuser.h>
#include <torch/csrc/jit/runtime/graph_executor.h>
#include <torch/cuda.h>
#include <torch/script.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>

#include "poly/action_encoding.hpp"

namespace poly::selfplay {

namespace {

// Edges per position in a captured graph; a batch with a larger node falls back to the plain path.
constexpr std::int64_t kGraphEdges = 256;

}  // namespace

// One CUDA graph of the module for a fixed batch size (inputs padded up to it): static input and
// output tensors on the GPU, replayed instead of launching ~60 kernels one by one (notebook
// 2026-11-13: ~5 ms of launch overhead per call, about 5x the arithmetic at batch 64).
struct CapturedGraph {
    std::int64_t batch = 0;
    std::unique_ptr<at::cuda::CUDAGraph> graph;
    std::vector<torch::Tensor> in;  // fields, flags, scalars, global, edges
    torch::Tensor logits;
    torch::Tensor value;
};

// One batcher's own GPU resources: a graph is replayed by one thread at a time, so each batcher has
// its own graphs, staging and stream.
struct TorchEvaluator::Pipeline {
    std::optional<c10::cuda::CUDAStream> stream;  // CUDA only
    std::vector<CapturedGraph> graphs;  // ascending batch sizes; empty: plain calls only
    // Graph path: pinned host staging for the inputs (max_batch rows, kGraphEdges edges) and the
    // outputs, so the copies are asynchronous and a batch synchronises once (notebook 2026-11-13).
    std::vector<torch::Tensor> staging;
    torch::Tensor out_logits;
    torch::Tensor out_value;
};

struct TorchEvaluator::Impl {
    torch::jit::Module module;
    torch::Device device{torch::kCPU};
    std::vector<Pipeline> pipelines;
};

struct TorchEvaluator::Request {
    const Features* features = nullptr;
    std::span<const std::int64_t> edges;
    std::vector<float>* logits = nullptr;
    float value = 0.0F;
    bool done = false;
    std::exception_ptr error;  // the batch's forward pass failed
};

namespace {

std::vector<torch::Tensor> zero_inputs(std::int64_t n, std::int64_t t, std::int64_t k,
                                       torch::Device d) {
    const auto o = torch::TensorOptions().device(d);
    return {torch::zeros({n, t, field::kCount}, o.dtype(torch::kUInt8)),
            torch::zeros({n, t}, o.dtype(torch::kUInt8)),
            torch::zeros({n, t, scalar::kCount}, o.dtype(torch::kFloat32)),
            torch::zeros({n, gfeat::kSize}, o.dtype(torch::kFloat32)),
            torch::zeros({n, k}, o.dtype(torch::kInt64))};
}

CapturedGraph capture(torch::jit::Module& m, std::int64_t batch, std::int64_t tiles,
                      torch::Device d) {
    CapturedGraph g{.batch = batch,
                    .graph = std::make_unique<at::cuda::CUDAGraph>(),
                    .in = zero_inputs(batch, tiles, kGraphEdges, d),
                    .logits = {},
                    .value = {}};
    const std::vector<torch::jit::IValue> in(g.in.begin(), g.in.end());
    const torch::InferenceMode guard;
    const c10::cuda::CUDAStream stream = c10::cuda::getStreamFromPool();
    {
        const c10::cuda::CUDAStreamGuard on(stream);
        for (int i = 0; i < 3; ++i) {  // warm-up outside the capture (cuDNN, allocator)
            (void)m.forward(in);
        }
        g.graph->capture_begin();
        const auto out = m.forward(in).toTuple();
        g.graph->capture_end();
        g.logits = out->elements()[0].toTensor();
        g.value = out->elements()[1].toTensor();
    }
    stream.synchronize();
    return g;
}

}  // namespace

TorchEvaluator::TorchEvaluator(const std::string& model, int side, int max_batch, int max_wait_us,
                               bool cuda, bool cuda_graphs, int pipelines)
    : impl_(std::make_unique<Impl>()),
      side_(side),
      max_batch_(max_batch),
      max_wait_us_(max_wait_us) {
    if (side <= 0 || side > kMaxSide || max_batch <= 0 || max_wait_us < 0 || pipelines <= 0) {
        throw std::invalid_argument("TorchEvaluator: bad side, batch, wait or pipelines");
    }
    if (cuda && !torch::cuda::is_available()) {
        throw std::runtime_error("TorchEvaluator: CUDA requested but not available");
    }
    impl_->device = cuda ? torch::Device(torch::kCUDA) : torch::Device(torch::kCPU);
    // TorchScript's optimizing executor fuses element-wise ops into CUDA kernels compiled at run
    // time by NVRTC, which fails in this setup (notebook 2026-11-13); the convolutions are cuDNN
    // either way.
    torch::jit::setGraphExecutorOptimize(false);
    torch::jit::setTensorExprFuserEnabled(false);
    impl_->module = torch::jit::load(model, impl_->device);
    impl_->module.eval();
    impl_->pipelines.resize(static_cast<std::size_t>(pipelines));
    for (Pipeline& p : impl_->pipelines) {
        if (cuda) {
            p.stream = c10::cuda::getStreamFromPool();
        }
        if (!cuda || !cuda_graphs) {
            continue;
        }
        const std::int64_t tiles = static_cast<std::int64_t>(side) * side;
        for (std::int64_t b = 8; b < max_batch; b *= 2) {
            p.graphs.push_back(capture(impl_->module, b, tiles, impl_->device));
        }
        p.graphs.push_back(capture(impl_->module, max_batch, tiles, impl_->device));
        p.staging = zero_inputs(max_batch, tiles, kGraphEdges, torch::kCPU);
        for (torch::Tensor& h : p.staging) {
            h = h.pin_memory();
        }
        const auto pinned = torch::TensorOptions().dtype(torch::kFloat32).pinned_memory(true);
        p.out_logits = torch::zeros({max_batch, kGraphEdges}, pinned);
        p.out_value = torch::zeros({max_batch}, pinned);
    }
    // POLY_EVAL_LOG=<seconds>: the batcher prints its statistics that often (long runs).
    if (const char* v = std::getenv("POLY_EVAL_LOG");
        v != nullptr) {  // NOLINT(concurrency-mt-unsafe)
        log_every_s_ = std::atoi(v);
    }
    started_ = last_log_ = std::chrono::steady_clock::now();
    for (Pipeline& p : impl_->pipelines) {
        batchers_.emplace_back([this, &p] { run(p); });
    }
}

TorchEvaluator::~TorchEvaluator() {
    {
        const std::scoped_lock lock(mu_);
        stop_ = true;
    }
    queued_.notify_all();
    for (std::thread& t : batchers_) {
        t.join();
    }
}

EvaluatorStats TorchEvaluator::stats() const {
    const std::scoped_lock lock(mu_);
    return stats_;
}

void TorchEvaluator::raw(const Features& f, std::span<const std::int64_t> edges,
                         std::vector<float>& logits, float& value) {
    check(f.side == side_, "TorchEvaluator: position of another map side");
    check(!edges.empty(), "TorchEvaluator: no edges");
    Request r{.features = &f, .edges = edges, .logits = &logits, .error = nullptr};
    std::unique_lock lock(mu_);
    queue_.push_back(&r);
    queued_.notify_one();
    done_.wait(lock, [&r] { return r.done; });
    if (r.error) {
        std::rethrow_exception(r.error);
    }
    value = r.value;
}

void TorchEvaluator::evaluate(const Observation& obs, std::span<const Action> actions,
                              std::vector<double>& priors, double& value) {
    thread_local Features f;
    thread_local std::vector<std::int64_t> edges;
    thread_local std::vector<float> logits;
    encode_features(obs, f);
    edges.clear();
    for (const Action& a : actions) {
        edges.push_back(network_index(encode_action(obs, a), side_));
    }
    float v = 0.0F;
    raw(f, edges, logits, v);
    // ADR-011 §2: softmax over the node's edges only.
    const float top = *std::ranges::max_element(logits);
    priors.resize(logits.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < logits.size(); ++i) {
        priors[i] = std::exp(static_cast<double>(logits[i] - top));
        sum += priors[i];
    }
    for (double& p : priors) {
        p /= sum;
    }
    value = static_cast<double>(v);
}

void TorchEvaluator::begin_search() {
    const std::scoped_lock lock(mu_);
    ++searching_;
}

void TorchEvaluator::end_search() {
    {
        const std::scoped_lock lock(mu_);
        --searching_;
    }
    queued_.notify_one();  // the waiting requests may now be all the searching threads have
}

void TorchEvaluator::log_stats() {
    if (log_every_s_ <= 0) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    EvaluatorStats st;
    {
        const std::scoped_lock lock(mu_);  // last_log_ is shared by the batchers
        if (now - last_log_ < std::chrono::seconds(log_every_s_)) {
            return;
        }
        last_log_ = now;
        st = stats_;
    }
    const double wall = std::chrono::duration<double>(now - started_).count();
    std::cerr << "evaluator: " << wall << " s: " << st.positions << " positions, " << st.batches
              << " batches (" << st.graph_batches << " graphs), mean batch "
              << static_cast<double>(st.positions) / static_cast<double>(std::max(1LL, st.batches))
              << ", " << st.gpu_seconds << " s in forward, " << st.fill_wait_seconds
              << " s waiting for a batch to fill, " << static_cast<double>(st.positions) / wall
              << " positions/s\n";
}

void TorchEvaluator::run(Pipeline& p) {
    std::optional<c10::cuda::CUDAStreamGuard> on;  // this batcher's copies and replays
    if (p.stream) {
        on.emplace(*p.stream);
    }
    std::vector<Request*> batch;
    for (;;) {
        {
            std::unique_lock lock(mu_);
            queued_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping
            }
            // Full when every searching thread not already in another batcher's batch waits (or
            // max_batch do); a caller outside begin_search (raw()) can't be counted, so with none
            // searching only max_batch is full.
            const auto full = [this] {
                const int want = searching_ > 0
                                     ? std::clamp(searching_ - in_flight_, 1, max_batch_)
                                     : max_batch_;
                return stop_ || std::cmp_greater_equal(queue_.size(), want);
            };
            if (!full()) {
                const auto w0 = std::chrono::steady_clock::now();
                queued_.wait_for(lock, std::chrono::microseconds(max_wait_us_), full);
                stats_.fill_wait_seconds +=
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
            }
            batch.clear();
            while (!queue_.empty() && std::cmp_less(batch.size(), max_batch_)) {
                batch.push_back(queue_.front());
                queue_.pop_front();
            }
            in_flight_ += static_cast<int>(batch.size());
        }
        const auto t0 = std::chrono::steady_clock::now();
        bool replayed = false;
        try {
            replayed = forward(p, batch);
        } catch (...) {  // every caller of this batch gets the error, the batcher keeps running
            const std::exception_ptr e = std::current_exception();
            for (Request* r : batch) {
                r->error = e;
            }
        }
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        {
            const std::scoped_lock lock(mu_);
            for (Request* r : batch) {
                r->done = true;
            }
            in_flight_ -= static_cast<int>(batch.size());
            ++stats_.batches;
            stats_.graph_batches += replayed ? 1 : 0;
            stats_.positions += static_cast<long long>(batch.size());
            stats_.gpu_seconds += secs;
        }
        done_.notify_all();
        queued_.notify_all();  // fewer in flight: another batcher's batch may now be full
        log_stats();
    }
}

namespace {

// Writes the batch's inputs into rows 0..n of host tensors (fields, flags, scalars, global, edges);
// edges get each request's own count, the rest of a row is left as it was.
template <typename Requests>
void fill(const Requests& batch, std::int64_t tiles, const std::vector<torch::Tensor>& host) {
    for (std::size_t i = 0; i < batch.size(); ++i) {
        const auto row = static_cast<std::int64_t>(i);
        const Features& f = *batch[i]->features;
        const auto e = batch[i]->edges;
        std::memcpy(host[0][row].data_ptr(), f.fields.data(),
                    static_cast<std::size_t>(tiles) * field::kCount);
        std::memcpy(host[1][row].data_ptr(), f.flags.data(), static_cast<std::size_t>(tiles));
        std::memcpy(host[2][row].data_ptr(), f.scalars.data(),
                    static_cast<std::size_t>(tiles) * scalar::kCount * sizeof(float));
        std::memcpy(host[3][row].data_ptr(), f.global.data(), sizeof(f.global));
        std::memcpy(host[4][row].data_ptr(), e.data(), e.size() * sizeof(std::int64_t));
    }
}

}  // namespace

bool TorchEvaluator::forward(Pipeline& p, std::vector<Request*>& batch) {
    const auto n = static_cast<std::int64_t>(batch.size());
    const std::int64_t t = static_cast<std::int64_t>(side_) * side_;
    std::int64_t k = 0;
    for (const Request* r : batch) {
        k = std::max(k, static_cast<std::int64_t>(r->edges.size()));
    }
    const torch::InferenceMode guard;
    const auto graph =
        std::ranges::find_if(p.graphs, [n](const CapturedGraph& g) { return g.batch >= n; });
    const bool replay = graph != p.graphs.end() && k <= kGraphEdges;
    torch::Tensor logits;  // [n, stride] on the host
    torch::Tensor values;
    if (replay) {
        // Pinned staging -> the graph's static inputs (rows past n hold stale but valid data whose
        // outputs are never read), replay, outputs -> pinned host; one synchronisation.
        fill(batch, t, p.staging);
        for (std::size_t i = 0; i < p.staging.size(); ++i) {
            graph->in[i].narrow(0, 0, n).copy_(p.staging[i].narrow(0, 0, n), true);
        }
        graph->graph->replay();
        p.out_logits.narrow(0, 0, n).copy_(graph->logits.narrow(0, 0, n), true);
        p.out_value.narrow(0, 0, n).copy_(graph->value.narrow(0, 0, n), true);
        c10::cuda::getCurrentCUDAStream().synchronize();
        logits = p.out_logits;
        values = p.out_value;
    } else {
        std::vector<torch::Tensor> host = zero_inputs(n, t, k, torch::kCPU);
        fill(batch, t, host);
        const torch::Device d = impl_->device;
        const std::vector<torch::jit::IValue> in = {host[0].to(d), host[1].to(d), host[2].to(d),
                                                    host[3].to(d), host[4].to(d)};
        const auto out = impl_->module.forward(in).toTuple();
        logits = out->elements()[0].toTensor().to(torch::kCPU).contiguous();
        values = out->elements()[1].toTensor().to(torch::kCPU).contiguous();
    }
    const std::int64_t stride = logits.size(1);
    const auto lp = std::span(logits.data_ptr<float>(), static_cast<std::size_t>(logits.numel()));
    const auto vp = std::span(values.data_ptr<float>(), static_cast<std::size_t>(values.numel()));
    for (std::int64_t i = 0; i < n; ++i) {
        Request& r = *batch[static_cast<std::size_t>(i)];
        const auto row = lp.subspan(static_cast<std::size_t>(i * stride), r.edges.size());
        r.logits->assign(row.begin(), row.end());
        r.value = vp[static_cast<std::size_t>(i)];
    }
    return replay;
}

}  // namespace poly::selfplay
