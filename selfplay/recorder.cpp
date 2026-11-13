#include "selfplay/recorder.hpp"

#include <utility>

#include "poly/action_encoding.hpp"

namespace poly::selfplay {

Action RecordingPuct::act(const Observation& obs, std::span<const Action> legal) {
    const Action chosen = inner_.act(obs, legal);
    if (inner_.last_fm_calls() == 0) {  // forced: nothing was searched
        return chosen;
    }
    const auto actions = inner_.root_actions();
    const auto summed = inner_.summed();
    check(actions.size() == summed.size(), "recorder: root actions and edge stats differ");
    const int side = obs.view().side;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
    edges.reserve(actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const int out = network_index(encode_action(obs, actions[i]), side);
        edges.emplace_back(static_cast<std::uint32_t>(out),
                           static_cast<std::uint32_t>(summed[i].visits));
    }
    encode_features(obs, features_);
    decisions_.push_back(make_decision(features_, obs.observer(), std::move(edges)));
    return chosen;
}

}  // namespace poly::selfplay
