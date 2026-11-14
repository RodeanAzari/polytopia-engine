#pragma once

// The agents our tools can run, by the names the tournament runner uses (ADR-007), and the seed
// mixing that derives each seat's agent seed from a game seed: shared by tools/tournament.cpp and
// tools/record.cpp, so a recorded game with a CSV row's game seed is that tournament game.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "search/agent.hpp"
#include "search/mc.hpp"
#include "search/mcts.hpp"
#include "search/osla.hpp"
#include "search/puct.hpp"
#include "search/random_agent.hpp"
#include "search/rhea.hpp"
#include "search/rule_based.hpp"
#ifdef POLY_TORCH
#include <map>
#include <mutex>

#include "selfplay/torch_evaluator.hpp"
#endif

namespace polytools {

inline std::uint64_t mix(std::uint64_t a, std::uint64_t b) {  // SplitMix64 of a combined value
    std::uint64_t z = (a * 0x9E3779B97F4A7C15ULL) + b + 0x632BE59BD9B4E019ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

// Seat i's (0 or 1) agent seed in the game with this game seed.
inline std::uint64_t agent_seed(std::uint64_t game_seed, int seat) {
    return mix(game_seed, static_cast<std::uint64_t>(seat) + 1);
}

// "PUCT:...:net=<model.ts>" (ADR-011 §2): `name` without the net item, and the model path ("" if
// none). The net item may be anywhere in the spec.
inline std::pair<std::string, std::string> split_net(const std::string& name) {
    const std::size_t at = name.find("net=");
    if (at == std::string::npos) {
        return {name, ""};
    }
    const std::size_t end = name.find(':', at);
    std::string model =
        name.substr(at + 4, end == std::string::npos ? std::string::npos : end - at - 4);
    std::string rest = name.substr(0, at) + (end == std::string::npos ? "" : name.substr(end + 1));
    if (rest.ends_with(':')) {
        rest.pop_back();
    }
    return {rest, model};
}

#ifdef POLY_TORCH
// One batched evaluator per (model, side), shared by every game of the process (ADR-012 §1).
inline poly::search::LeafEvaluator* shared_evaluator(const std::string& model, int side) {
    static std::mutex mu;
    static std::map<std::pair<std::string, int>, std::unique_ptr<poly::selfplay::TorchEvaluator>>
        evaluators;
    const std::scoped_lock lock(mu);
    auto& e = evaluators[{model, side}];
    if (!e) {
        e = std::make_unique<poly::selfplay::TorchEvaluator>(model, side, 64, 1000);
    }
    return e.get();
}
#endif

// `side`: the map side, which network agents need (their evaluator is built for it).
inline std::unique_ptr<poly::search::Agent> make_agent(const std::string& full_name,
                                                       std::uint64_t seed, int side = 0) {
    const auto [name, model] = split_net(full_name);
    poly::search::LeafEvaluator* net = nullptr;
    if (!model.empty()) {
        if (name != "PUCT" && !name.starts_with("PUCT:")) {
            throw std::runtime_error("only PUCT agents take net=: " + full_name);
        }
#ifdef POLY_TORCH
        if (side <= 0) {
            throw std::runtime_error("a network agent needs the map side: " + full_name);
        }
        net = shared_evaluator(model, side);
#else
        (void)side;
        throw std::runtime_error("net= needs a build with POLY_TORCH (make build-nn): " +
                                 full_name);
#endif
    }
    if (name == "Random") {
        return std::make_unique<poly::search::RandomAgent>(seed);
    }
    if (name == "RuleBased") {
        return std::make_unique<poly::search::RuleBasedAgent>(seed);
    }
    if (name == "OSLA") {
        return std::make_unique<poly::search::OslaAgent>(seed);
    }
    if (name == "MC") {
        return std::make_unique<poly::search::McAgent>(seed);
    }
    if (name == "MCTS") {
        return std::make_unique<poly::search::MctsAgent>(seed);
    }
    if (name == "RHEA") {
        return std::make_unique<poly::search::RheaAgent>(seed);
    }
    if (name == "PUCT" || name.starts_with("PUCT:")) {  // ours, ADR-008; e.g. PUCT:c=1.5:vision=own
        const std::string_view spec =
            name.size() > 5 ? std::string_view(name).substr(5) : std::string_view{};
        return std::make_unique<poly::search::PuctAgent>(
            seed, poly::search::parse_puct_params(spec), net);
    }
    throw std::runtime_error("unknown agent: " + name);
}

// Throws if `name` is not an agent make_agent can build (a network agent's model isn't loaded).
inline void check_agent(const std::string& name) {
    const auto [base, model] = split_net(name);
#ifndef POLY_TORCH
    if (!model.empty()) {
        throw std::runtime_error("net= needs a build with POLY_TORCH (make build-nn): " + name);
    }
#endif
    (void)make_agent(base, 0);
}

}  // namespace polytools
