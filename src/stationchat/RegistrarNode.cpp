
#include "RegistrarNode.hpp"

#include "StationChatConfig.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>

namespace {

constexpr std::chrono::seconds kBaseBlacklistDuration{5};
constexpr std::chrono::seconds kMaxBlacklistDuration{60};

} // namespace

RegistrarNode::RegistrarNode(StationChatConfig& config)
    : Node(this, config.registrarAddress, config.registrarPort, config.bindToIp)
    , config_{config} {
    RebuildClusterView();
}

RegistrarNode::~RegistrarNode() {}

StationChatConfig& RegistrarNode::GetConfig() {
    return config_;
}

GatewayClusterEndpoint RegistrarNode::SelectGatewayEndpoint(
    const std::string& preferredAddress, uint16_t preferredPort) {
    std::lock_guard<std::mutex> lock(endpointMutex_);

    if (weightedGatewayEndpoints_.empty()) {
        return {config_.gatewayAddress, config_.gatewayPort, 1};
    }

    PruneExpiredBlacklistLocked();

    const bool hasPreferred = !preferredAddress.empty() && preferredPort != 0;
    if (hasPreferred) {
        if (auto* preferred = FindEndpointHealthLocked(preferredAddress, preferredPort)) {
            if (!IsEndpointBlacklistedLocked(*preferred)) {
                return preferred->endpoint;
            }
        }
    }

    const auto index = nextGatewayIndex_.fetch_add(1, std::memory_order_relaxed);
    const auto total = weightedGatewayEndpoints_.size();

    GatewayClusterEndpoint selectedEndpoint{config_.gatewayAddress, config_.gatewayPort, 1};
    bool found = false;

    for (std::size_t offset = 0; offset < total; ++offset) {
        const auto& candidate = weightedGatewayEndpoints_[(index + offset) % total];
        EnsureEndpointEntryLocked(candidate);
        auto* health = FindEndpointHealthLocked(candidate.address, candidate.port);
        if (health == nullptr) {
            selectedEndpoint = candidate;
            found = true;
            break;
        }

        if (!IsEndpointBlacklistedLocked(*health)) {
            selectedEndpoint = candidate;
            found = true;
            break;
        }
    }

    if (!found) {
        auto best = std::min_element(endpointHealth_.begin(), endpointHealth_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.second.blacklistUntil < rhs.second.blacklistUntil;
            });

        if (best != endpointHealth_.end()) {
            best->second.blacklistUntil = std::chrono::steady_clock::time_point{};
            selectedEndpoint = best->second.endpoint;
            found = true;
        }
    }

    if (!found) {
        selectedEndpoint = weightedGatewayEndpoints_[index % total];
    }

    return selectedEndpoint;
}

void RegistrarNode::OnTick() {}

void RegistrarNode::RebuildClusterView() {
    // Clamp weights to avoid exploding the weighted view if an extremely large
    // value is configured. The registrar still honors relative weights while
    // keeping the in-memory representation bounded.
    static constexpr uint16_t kMaxWeight = 100;

    std::vector<GatewayClusterEndpoint> weightedEndpoints;
    std::vector<GatewayClusterEndpoint> uniqueEndpoints;

    const auto appendEndpoint = [&weightedEndpoints, &uniqueEndpoints](const GatewayClusterEndpoint& endpoint) {
        weightedEndpoints.push_back(endpoint);
        if (std::none_of(uniqueEndpoints.begin(), uniqueEndpoints.end(),
                [&endpoint](const GatewayClusterEndpoint& existing) {
                    return existing.Matches(endpoint.address, endpoint.port);
                })) {
            uniqueEndpoints.push_back(endpoint);
        }
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

    if (weightedEndpoints.empty()) {
        appendEndpoint({config_.gatewayAddress, config_.gatewayPort, 1});
    }

    {
        std::lock_guard<std::mutex> lock(endpointMutex_);
        weightedGatewayEndpoints_ = std::move(weightedEndpoints);

        EndpointHealthMap updatedHealth;
        for (const auto& endpoint : uniqueEndpoints) {
            const auto key = MakeEndpointKey(endpoint.address, endpoint.port);
            const auto existing = endpointHealth_.find(key);
            if (existing != endpointHealth_.end()) {
                auto health = existing->second;
                health.endpoint = endpoint;
                updatedHealth.emplace(key, std::move(health));
            } else {
                updatedHealth.emplace(key, EndpointHealth{endpoint});
            }
        }

        endpointHealth_.swap(updatedHealth);
    }

    nextGatewayIndex_.store(0, std::memory_order_relaxed);
}

void RegistrarNode::ReportGatewayFailure(const std::string& address, uint16_t port) {
    if (address.empty() || port == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(endpointMutex_);
    if (auto* health = FindEndpointHealthLocked(address, port)) {
        MarkFailureLocked(*health);
    }
}

void RegistrarNode::ReportGatewaySuccess(const std::string& address, uint16_t port) {
    if (address.empty() || port == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(endpointMutex_);
    if (auto* health = FindEndpointHealthLocked(address, port)) {
        MarkSuccessLocked(*health);
    }
}

bool RegistrarNode::IsEndpointBlacklistedLocked(const EndpointHealth& health) const {
    if (health.blacklistUntil == std::chrono::steady_clock::time_point{}) {
        return false;
    }

    return std::chrono::steady_clock::now() < health.blacklistUntil;
}

void RegistrarNode::PruneExpiredBlacklistLocked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto& [key, health] : endpointHealth_) {
        if (health.blacklistUntil != std::chrono::steady_clock::time_point{}
            && health.blacklistUntil <= now) {
            health.blacklistUntil = std::chrono::steady_clock::time_point{};
            health.failureCount = 0;
        }
    }
}

RegistrarNode::EndpointHealth* RegistrarNode::FindEndpointHealthLocked(
    const std::string& address, uint16_t port) {
    if (address.empty() || port == 0) {
        return nullptr;
    }

    const auto key = MakeEndpointKey(address, port);
    auto iter = endpointHealth_.find(key);
    if (iter != endpointHealth_.end()) {
        return &iter->second;
    }

    for (const auto& endpoint : weightedGatewayEndpoints_) {
        if (endpoint.Matches(address, port)) {
            auto [insertedIter, _] = endpointHealth_.emplace(key, EndpointHealth{endpoint});
            return &insertedIter->second;
        }
    }

    return nullptr;
}

void RegistrarNode::EnsureEndpointEntryLocked(const GatewayClusterEndpoint& endpoint) {
    const auto key = MakeEndpointKey(endpoint.address, endpoint.port);
    auto iter = endpointHealth_.find(key);
    if (iter == endpointHealth_.end()) {
        endpointHealth_.emplace(key, EndpointHealth{endpoint});
    } else {
        iter->second.endpoint = endpoint;
    }
}

void RegistrarNode::MarkFailureLocked(EndpointHealth& health) {
    const auto now = std::chrono::steady_clock::now();
    const auto cappedFailures
        = std::min<std::size_t>(health.failureCount + 1, static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()));
    health.failureCount = cappedFailures;

    auto penalty = kBaseBlacklistDuration * static_cast<int64_t>(health.failureCount);
    if (penalty > kMaxBlacklistDuration) {
        penalty = kMaxBlacklistDuration;
    }

    health.blacklistUntil = now + penalty;
}

void RegistrarNode::MarkSuccessLocked(EndpointHealth& health) {
    health.failureCount = 0;
    health.blacklistUntil = std::chrono::steady_clock::time_point{};
}

std::string RegistrarNode::MakeEndpointKey(const std::string& address, uint16_t port) {
    std::string key = address;
    key.push_back('#');
    key += std::to_string(port);
    return key;
}

