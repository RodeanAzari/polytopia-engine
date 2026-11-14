// The runner's agent names (tools/agents.hpp): the network item of "PUCT:...:net=<model>" is split
// off wherever it sits, and in a build without LibTorch a network agent is refused loudly rather
// than silently played without its network.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>

#include "tools/agents.hpp"

namespace {

using Split = std::pair<std::string, std::string>;

TEST(AgentNames, NetworkItemIsSplitOff) {
    EXPECT_EQ(polytools::split_net("PUCT"), Split("PUCT", ""));
    EXPECT_EQ(polytools::split_net("PUCT:net=a/b.ts"), Split("PUCT", "a/b.ts"));
    EXPECT_EQ(polytools::split_net("PUCT:net=m.ts:budget=400"), Split("PUCT:budget=400", "m.ts"));
    EXPECT_EQ(polytools::split_net("PUCT:K=0:net=m.ts:macro=0"), Split("PUCT:K=0:macro=0", "m.ts"));
    EXPECT_EQ(polytools::split_net("PUCT:c=2:net=m.ts"), Split("PUCT:c=2", "m.ts"));
}

TEST(AgentNames, CheckRejectsUnknownAndBadSpecs) {
    EXPECT_NO_THROW(polytools::check_agent("PUCT:budget=400"));
    EXPECT_NO_THROW(polytools::check_agent("RHEA"));
    EXPECT_THROW(polytools::check_agent("Nope"), std::runtime_error);
    EXPECT_THROW(polytools::check_agent("PUCT:budget=0"), std::invalid_argument);
}

#ifndef POLY_TORCH
TEST(AgentNames, NetworkAgentsNeedTheLibTorchBuild) {
    EXPECT_THROW(polytools::check_agent("PUCT:net=m.ts"), std::runtime_error);
    EXPECT_THROW((void)polytools::make_agent("PUCT:net=m.ts", 1, 11), std::runtime_error);
}
#endif

TEST(AgentNames, OnlyPuctTakesANetwork) {
    EXPECT_THROW((void)polytools::make_agent("RHEA:net=m.ts", 1, 11), std::runtime_error);
}

}  // namespace
