
#pragma once

#include "Node.hpp"
#include "RegistrarClient.hpp"
#include "StationChatConfig.hpp"

#include <atomic>
#include <vector>

class RegistrarNode : public Node<RegistrarNode, RegistrarClient> {
public:
    explicit RegistrarNode(StationChatConfig& config);
    ~RegistrarNode();

    StationChatConfig& GetConfig();
    GatewayClusterEndpoint SelectGatewayEndpoint();

private:
    void OnTick() override;
    void RebuildClusterView();

    StationChatConfig& config_;
    std::vector<GatewayClusterEndpoint> weightedGatewayEndpoints_;
    std::atomic<size_t> nextGatewayIndex_{0};
};
