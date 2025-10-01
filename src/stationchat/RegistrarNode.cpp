
#include "RegistrarNode.hpp"

#include "StationChatConfig.hpp"

#include <algorithm>

RegistrarNode::RegistrarNode(StationChatConfig& config)
    : Node(this, config.registrarAddress, config.registrarPort, config.bindToIp)
    , config_{config} {
    RebuildClusterView();
}

RegistrarNode::~RegistrarNode() {}

StationChatConfig& RegistrarNode::GetConfig() {
    return config_;
}

GatewayClusterEndpoint RegistrarNode::SelectGatewayEndpoint() {
    if (weightedGatewayEndpoints_.empty()) {
        return {config_.gatewayAddress, config_.gatewayPort, 1};
    }

    auto index = nextGatewayIndex_.fetch_add(1, std::memory_order_relaxed);
    const auto& endpoint = weightedGatewayEndpoints_[index % weightedGatewayEndpoints_.size()];
    return endpoint;
}

void RegistrarNode::OnTick() {}

void RegistrarNode::RebuildClusterView() {
    // Clamp weights to avoid exploding the weighted view if an extremely large
    // value is configured. The registrar still honors relative weights while
    // keeping the in-memory representation bounded.
    static constexpr uint16_t kMaxWeight = 100;

    weightedGatewayEndpoints_.clear();

    const auto appendEndpoint = [this](const GatewayClusterEndpoint& endpoint) {
        weightedGatewayEndpoints_.push_back(endpoint);
    };

    if (!config_.gatewayCluster.empty()) {
        for (const auto& endpoint : config_.gatewayCluster) {
            const auto weight = std::max<uint16_t>(static_cast<uint16_t>(1), endpoint.weight);
            const auto repetitions = std::min<uint16_t>(weight, kMaxWeight);
            for (uint16_t repeat = 0; repeat < repetitions; ++repeat) {
                appendEndpoint(endpoint);
            }
        }
    }

    if (weightedGatewayEndpoints_.empty()) {
        appendEndpoint({config_.gatewayAddress, config_.gatewayPort, 1});
    }

    nextGatewayIndex_.store(0, std::memory_order_relaxed);
}

