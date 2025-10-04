
#pragma once

#include "Node.hpp"
#include "RegistrarClient.hpp"
#include "StationChatConfig.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class RegistrarNode : public Node<RegistrarNode, RegistrarClient> {
public:
    explicit RegistrarNode(StationChatConfig& config);
    ~RegistrarNode();

    StationChatConfig& GetConfig();

    GatewayClusterEndpoint SelectGatewayEndpoint(
        const std::string& preferredAddress = {}, uint16_t preferredPort = 0);

    void ReportGatewayFailure(const std::string& address, uint16_t port);
    void ReportGatewaySuccess(const std::string& address, uint16_t port);

private:
    void OnTick() override;
    void RebuildClusterView();

    struct EndpointHealth {
        GatewayClusterEndpoint endpoint;
        std::size_t failureCount{0};
        std::chrono::steady_clock::time_point blacklistUntil{};
    };

    using EndpointHealthMap = std::unordered_map<std::string, EndpointHealth>;

    bool IsEndpointBlacklistedLocked(const EndpointHealth& health) const;
    void PruneExpiredBlacklistLocked();
    EndpointHealth* FindEndpointHealthLocked(const std::string& address, uint16_t port);
    void EnsureEndpointEntryLocked(const GatewayClusterEndpoint& endpoint);
    void MarkFailureLocked(EndpointHealth& health);
    void MarkSuccessLocked(EndpointHealth& health);
    static std::string MakeEndpointKey(const std::string& address, uint16_t port);

    StationChatConfig& config_;
    std::vector<GatewayClusterEndpoint> weightedGatewayEndpoints_;
    EndpointHealthMap endpointHealth_;
    std::mutex endpointMutex_;
    std::atomic<size_t> nextGatewayIndex_{0};
};
