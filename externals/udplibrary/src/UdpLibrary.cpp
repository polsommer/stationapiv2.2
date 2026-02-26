#include "UdpLibrary.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr std::size_t UDP_ADDRESS_BUFFER = 256;
constexpr int INVALID_SOCKET_FD = -1;
constexpr std::size_t MAX_UDP_PACKET_SIZE = 65535;

struct ConnectionState {
    int socketFd = INVALID_SOCKET_FD;
    sockaddr_in destination{};
    bool hasDestination = false;
};

struct ManagerState {
    int socketFd = INVALID_SOCKET_FD;
    std::unordered_map<std::string, UdpConnection*> peers;
};

std::mutex g_stateMutex;
std::unordered_map<UdpConnection*, ConnectionState> g_connectionStates;
std::unordered_map<UdpManager*, ManagerState> g_managerStates;

std::string IpToString(const in_addr& address) {
    char buffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &address, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return buffer;
}

std::string EndpointToKey(const sockaddr_in& endpoint) {
    auto ip = IpToString(endpoint.sin_addr);
    if (ip.empty()) {
        return {};
    }

    return ip + ":" + std::to_string(ntohs(endpoint.sin_port));
}

bool FillSockaddr(const std::string& address, uint16_t port, sockaddr_in* endpoint) {
    if (endpoint == nullptr) {
        return false;
    }

    std::memset(endpoint, 0, sizeof(sockaddr_in));
    endpoint->sin_family = AF_INET;
    endpoint->sin_port = htons(port);

    return inet_pton(AF_INET, address.c_str(), &endpoint->sin_addr) == 1;
}

void LogSocketError(const std::string& prefix) {
    std::cerr << "[udplibrary] " << prefix << ": " << std::strerror(errno) << "\n";
}
} // namespace

UdpIpAddress::UdpIpAddress() : address_{"0.0.0.0"} {}
UdpIpAddress::UdpIpAddress(std::string address) : address_{std::move(address)} {}

const char* UdpIpAddress::GetAddress(char* buffer) const {
    if (buffer == nullptr) {
        return address_.c_str();
    }

    std::fill_n(buffer, UDP_ADDRESS_BUFFER, '\0');
    auto to_copy = std::min(address_.size(), UDP_ADDRESS_BUFFER - 1);
    std::copy_n(address_.data(), to_copy, buffer);
    buffer[to_copy] = '\0';
    return buffer;
}

void UdpIpAddress::SetAddress(std::string address) { address_ = std::move(address); }

UdpConnection::UdpConnection()
    : refCount_{1}
    , status_{cStatusConnected}
    , handler_{nullptr}
    , destination_{"0.0.0.0"}
    , destinationPort_{0} {
    std::lock_guard<std::mutex> lock{g_stateMutex};
    g_connectionStates[this] = ConnectionState{};
}

void UdpConnection::AddRef() { ++refCount_; }

void UdpConnection::Release() {
    if (--refCount_ == 0) {
        {
            std::lock_guard<std::mutex> lock{g_stateMutex};
            g_connectionStates.erase(this);
        }
        delete this;
    }
}

void UdpConnection::SetHandler(UdpConnectionHandler* handler) { handler_ = handler; }
void UdpConnection::Disconnect() { status_ = cStatusDisconnected; }

void UdpConnection::Send(int, const char* data, uint32_t length) {
    if (status_ != cStatusConnected || data == nullptr || length == 0) {
        std::cerr << "[udplibrary] dropping send request due to invalid connection/data state\n";
        return;
    }

    ConnectionState stateCopy;
    {
        std::lock_guard<std::mutex> lock{g_stateMutex};
        const auto stateIter = g_connectionStates.find(this);
        if (stateIter == g_connectionStates.end() || !stateIter->second.hasDestination ||
            stateIter->second.socketFd == INVALID_SOCKET_FD) {
            std::cerr << "[udplibrary] send failed: destination or socket is not configured\n";
            return;
        }

        stateCopy = stateIter->second;
    }

    const auto bytes = sendto(stateCopy.socketFd, data, length, 0,
        reinterpret_cast<const sockaddr*>(&stateCopy.destination), sizeof(stateCopy.destination));
    if (bytes < 0 || static_cast<uint32_t>(bytes) != length) {
        LogSocketError("sendto failed");
    }
}

UdpConnection::Status UdpConnection::GetStatus() const { return status_; }
const UdpIpAddress& UdpConnection::GetDestinationIp() const { return destination_; }
uint16_t UdpConnection::GetDestinationPort() const { return destinationPort_; }

void UdpConnection::SetDestination(const std::string& address, uint16_t port) {
    destination_.SetAddress(address);
    destinationPort_ = port;

    std::lock_guard<std::mutex> lock{g_stateMutex};
    auto stateIter = g_connectionStates.find(this);
    if (stateIter == g_connectionStates.end()) {
        return;
    }

    if (!FillSockaddr(address, port, &stateIter->second.destination)) {
        stateIter->second.hasDestination = false;
        std::cerr << "[udplibrary] invalid destination address: " << address << "\n";
        return;
    }

    stateIter->second.hasDestination = true;
}

void UdpConnection::SimulateIncoming(const uchar* data, int length) {
    if (handler_ == nullptr || data == nullptr || length <= 0 || status_ != cStatusConnected) {
        return;
    }

    handler_->OnRoutePacket(this, data, length);
}

UdpManager::UdpManager(const Params* params)
    : handler_{params != nullptr ? params->handler : nullptr}
    , listenPort_{static_cast<uint16_t>(params != nullptr ? params->port : 0)} {
    int socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        LogSocketError("failed to create UDP socket");
        return;
    }

    int reuse = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LogSocketError("failed to set SO_REUSEADDR");
    }

    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags < 0 || fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LogSocketError("failed to set UDP socket non-blocking");
    }

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(listenPort_);

    if (params != nullptr && params->bindIpAddress[0] != '\0') {
        if (inet_pton(AF_INET, params->bindIpAddress, &bindAddr.sin_addr) != 1) {
            std::cerr << "[udplibrary] invalid bind IP address: " << params->bindIpAddress << "\n";
            close(socketFd);
            return;
        }
    } else {
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(socketFd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
        LogSocketError("failed to bind UDP socket");
        close(socketFd);
        return;
    }

    std::lock_guard<std::mutex> lock{g_stateMutex};
    g_managerStates[this] = ManagerState{socketFd, {}};
}

UdpManager::~UdpManager() {
    std::vector<UdpConnection*> peers;
    int socketFd = INVALID_SOCKET_FD;

    {
        std::lock_guard<std::mutex> lock{g_stateMutex};
        auto stateIter = g_managerStates.find(this);
        if (stateIter == g_managerStates.end()) {
            return;
        }

        socketFd = stateIter->second.socketFd;
        for (auto& [_, connection] : stateIter->second.peers) {
            peers.push_back(connection);
        }
        g_managerStates.erase(stateIter);
    }

    for (auto* connection : peers) {
        {
            std::lock_guard<std::mutex> lock{g_stateMutex};
            auto connStateIter = g_connectionStates.find(connection);
            if (connStateIter != g_connectionStates.end()) {
                connStateIter->second.socketFd = INVALID_SOCKET_FD;
            }
        }
        connection->Disconnect();
        connection->Release();
    }

    if (socketFd != INVALID_SOCKET_FD) {
        close(socketFd);
    }
}

void UdpManager::Release() { delete this; }

void UdpManager::GiveTime() {
    int socketFd = INVALID_SOCKET_FD;
    {
        std::lock_guard<std::mutex> lock{g_stateMutex};
        auto stateIter = g_managerStates.find(this);
        if (stateIter == g_managerStates.end()) {
            return;
        }
        socketFd = stateIter->second.socketFd;
    }

    if (socketFd == INVALID_SOCKET_FD) {
        return;
    }

    pollfd pfd{};
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0) {
        std::vector<uchar> packet(MAX_UDP_PACKET_SIZE);
        sockaddr_in srcAddr{};
        socklen_t srcLen = sizeof(srcAddr);

        auto recvLength = recvfrom(socketFd, packet.data(), packet.size(), 0,
            reinterpret_cast<sockaddr*>(&srcAddr), &srcLen);
        if (recvLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LogSocketError("recvfrom failed");
            }
            break;
        }

        if (recvLength == 0) {
            std::cerr << "[udplibrary] dropping malformed empty UDP packet\n";
            continue;
        }

        const auto peerKey = EndpointToKey(srcAddr);
        const auto peerIp = IpToString(srcAddr.sin_addr);
        const auto peerPort = static_cast<uint16_t>(ntohs(srcAddr.sin_port));
        if (peerKey.empty() || peerIp.empty()) {
            std::cerr << "[udplibrary] failed to parse source endpoint for UDP packet\n";
            continue;
        }

        UdpConnection* connection = nullptr;
        bool isNewConnection = false;

        {
            std::lock_guard<std::mutex> lock{g_stateMutex};
            auto managerIter = g_managerStates.find(this);
            if (managerIter == g_managerStates.end()) {
                return;
            }

            auto peerIter = managerIter->second.peers.find(peerKey);
            if (peerIter != managerIter->second.peers.end()) {
                connection = peerIter->second;
            }
        }

        if (connection == nullptr) {
            auto* newConnection = new UdpConnection();
            newConnection->SetDestination(peerIp, peerPort);

            {
                std::lock_guard<std::mutex> lock{g_stateMutex};
                auto managerIter = g_managerStates.find(this);
                if (managerIter == g_managerStates.end()) {
                    newConnection->Release();
                    return;
                }

                auto [peerIter, inserted] =
                    managerIter->second.peers.emplace(peerKey, newConnection);
                connection = peerIter->second;
                isNewConnection = inserted;

                if (inserted) {
                    auto connStateIter = g_connectionStates.find(newConnection);
                    if (connStateIter != g_connectionStates.end()) {
                        connStateIter->second.socketFd = socketFd;
                    }
                    newConnection->AddRef();
                }
            }

            newConnection->Release();
        }

        if (connection == nullptr || connection->GetStatus() != UdpConnection::cStatusConnected) {
            continue;
        }

        if (isNewConnection && handler_ != nullptr) {
            handler_->OnConnectRequest(connection);
        }

        connection->SimulateIncoming(packet.data(), static_cast<int>(recvLength));
    }
}

UdpConnection* UdpManager::CreateConnection() {
    auto* connection = new UdpConnection();
    connection->SetDestination("127.0.0.1", listenPort_);

    std::lock_guard<std::mutex> lock{g_stateMutex};
    auto managerIter = g_managerStates.find(this);
    if (managerIter != g_managerStates.end()) {
        auto connStateIter = g_connectionStates.find(connection);
        if (connStateIter != g_connectionStates.end()) {
            connStateIter->second.socketFd = managerIter->second.socketFd;
        }
    }

    return connection;
}
