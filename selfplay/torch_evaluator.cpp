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
#include <cstring>
#include <exception>
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

struct TorchEvaluator::Impl {
    torch::jit::Module module;
    torch::Device device{torch::kCPU};
    std::vector<CapturedGraph> graphs;  // ascending batch sizes; empty: plain calls only
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
                               bool cuda, bool cuda_graphs)
    : impl_(std::make_unique<Impl>()),
      side_(side),
      max_batch_(max_batch),
      max_wait_us_(max_wait_us) {
    if (side <= 0 || side > kMaxSide || max_batch <= 0 || max_wait_us < 0) {
        throw std::invalid_argument("TorchEvaluator: bad side, batch or wait");
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
    if (cuda && cuda_graphs) {
        const std::int64_t tiles = static_cast<std::int64_t>(side) * side;
        for (std::int64_t b = 8; b < max_batch; b *= 2) {
            impl_->graphs.push_back(capture(impl_->module, b, tiles, impl_->device));
        }
        impl_->graphs.push_back(capture(impl_->module, max_batch, tiles, impl_->device));
    }
    batcher_ = std::thread([this] { run(); });
}

TorchEvaluator::~TorchEvaluator() {
    {
        const std::scoped_lock lock(mu_);
        stop_ = true;
    }
    queued_.notify_all();
    batcher_.join();
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

void TorchEvaluator::run() {
    std::vector<Request*> batch;
    for (;;) {
        {
            std::unique_lock lock(mu_);
            queued_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping
            }
            if (std::cmp_less(queue_.size(), max_batch_)) {
                queued_.wait_for(lock, std::chrono::microseconds(max_wait_us_), [this] {
                    return stop_ || std::cmp_greater_equal(queue_.size(), max_batch_);
                });
            }
            batch.clear();
            while (!queue_.empty() && std::cmp_less(batch.size(), max_batch_)) {
                batch.push_back(queue_.front());
                queue_.pop_front();
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        bool replayed = false;
        try {
            replayed = forward(batch);
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
            ++stats_.batches;
            stats_.graph_batches += replayed ? 1 : 0;
            stats_.positions += static_cast<long long>(batch.size());
            stats_.gpu_seconds += secs;
        }
        done_.notify_all();
    }
}

bool TorchEvaluator::forward(std::vector<Request*>& batch) {
    const auto n = static_cast<std::int64_t>(batch.size());
    const std::int64_t t = static_cast<std::int64_t>(side_) * side_;
    std::int64_t k = 0;
    for (const Request* r : batch) {
        k = std::max(k, static_cast<std::int64_t>(r->edges.size()));
    }
    auto fields = torch::empty({n, t, field::kCount}, torch::kUInt8);
    auto flags = torch::empty({n, t}, torch::kUInt8);
    auto scalars = torch::empty({n, t, scalar::kCount}, torch::kFloat32);
    auto global = torch::empty({n, gfeat::kSize}, torch::kFloat32);
    auto edges = torch::zeros({n, k}, torch::kInt64);
    for (std::int64_t i = 0; i < n; ++i) {
        const Request& r = *batch[static_cast<std::size_t>(i)];
        const Features& f = *r.features;
        std::memcpy(fields[i].data_ptr(), f.fields.data(),
                    static_cast<std::size_t>(t) * field::kCount);
        std::memcpy(flags[i].data_ptr(), f.flags.data(), static_cast<std::size_t>(t));
        std::memcpy(scalars[i].data_ptr(), f.scalars.data(),
                    static_cast<std::size_t>(t) * scalar::kCount * sizeof(float));
        std::memcpy(global[i].data_ptr(), f.global.data(), sizeof(f.global));
        std::memcpy(edges[i].data_ptr(), r.edges.data(), r.edges.size() * sizeof(std::int64_t));
    }
    const torch::Device d = impl_->device;
    const torch::InferenceMode guard;
    torch::Tensor logits;
    torch::Tensor values;
    const auto graph =
        std::ranges::find_if(impl_->graphs, [n](const CapturedGraph& g) { return g.batch >= n; });
    const bool replay = graph != impl_->graphs.end() && k <= kGraphEdges;
    if (replay) {
        // Copy into the graph's static inputs (rows past n keep zeros from an earlier batch or
        // stale rows: their outputs are never read), replay, read the first n rows.
        const std::vector<torch::Tensor> host = {fields, flags, scalars, global, edges};
        for (std::size_t i = 0; i < host.size(); ++i) {
            auto dst = graph->in[i].narrow(0, 0, n);
            if (i == host.size() - 1) {
                dst = dst.narrow(1, 0, k);
            }
            dst.copy_(host[i]);
        }
        graph->graph->replay();
        logits = graph->logits.narrow(0, 0, n).narrow(1, 0, k).to(torch::kCPU).contiguous();
        values = graph->value.narrow(0, 0, n).to(torch::kCPU).contiguous();
    } else {
        const std::vector<torch::jit::IValue> in = {fields.to(d), flags.to(d), scalars.to(d),
                                                    global.to(d), edges.to(d)};
        const auto out = impl_->module.forward(in).toTuple();
        logits = out->elements()[0].toTensor().to(torch::kCPU).contiguous();
        values = out->elements()[1].toTensor().to(torch::kCPU).contiguous();
    }
    const float* lp = logits.data_ptr<float>();
    const float* vp = values.data_ptr<float>();
    for (std::int64_t i = 0; i < n; ++i) {
        Request& r = *batch[static_cast<std::size_t>(i)];
        const auto row = std::span(lp, static_cast<std::size_t>(n * k))
                             .subspan(static_cast<std::size_t>(i * k), r.edges.size());
        r.logits->assign(row.begin(), row.end());
        r.value = std::span(vp, static_cast<std::size_t>(n))[static_cast<std::size_t>(i)];
    }
    return replay;
}

}  // namespace poly::selfplay
