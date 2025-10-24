#include "UdpLibrary.hpp"

#include <algorithm>
#include <utility>

UdpIpAddress::UdpIpAddress() : address_{"0.0.0.0"} {}

UdpIpAddress::UdpIpAddress(std::string address) : address_{std::move(address)} {}

const char* UdpIpAddress::GetAddress(char* buffer) const {
    if (buffer == nullptr) {
        return nullptr;
    }

    std::size_t copyLength = std::min(address_.size(), static_cast<std::size_t>(63));
    std::memcpy(buffer, address_.data(), copyLength);
    buffer[copyLength] = '\0';
    return buffer;
}

void UdpIpAddress::SetAddress(std::string address) {
    address_ = std::move(address);
}

UdpConnection::UdpConnection()
    : refCount_{1}
    , handler_{nullptr}
    , destinationIp_{}
    , destinationPort_{0}
    , status_{cStatusDisconnected} {}

UdpConnection::UdpConnection(std::string destinationAddress, uint16_t destinationPort)
    : refCount_{1}
    , handler_{nullptr}
    , destinationIp_{std::move(destinationAddress)}
    , destinationPort_{destinationPort}
    , status_{cStatusConnected} {}

void UdpConnection::AddRef() {
    refCount_.fetch_add(1, std::memory_order_relaxed);
}

void UdpConnection::Release() {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;
    }
}

void UdpConnection::SetHandler(UdpConnectionHandler* handler) {
    handler_ = handler;
}

void UdpConnection::Disconnect() {
    status_ = cStatusDisconnected;
}

void UdpConnection::Send(uint8_t, const char*, uint32_t) {
    // Stub implementation intentionally does nothing.
}

const UdpIpAddress& UdpConnection::GetDestinationIp() const {
    return destinationIp_;
}

uint16_t UdpConnection::GetDestinationPort() const {
    return destinationPort_;
}

void UdpConnection::SetDestination(std::string address, uint16_t port) {
    destinationIp_.SetAddress(std::move(address));
    destinationPort_ = port;
}

UdpConnection::Status UdpConnection::GetStatus() const {
    return status_;
}

void UdpConnection::SetStatus(Status status) {
    status_ = status;
}

UdpManager::UdpManager(const Params* params)
    : refCount_{1}
    , handler_{params != nullptr ? params->handler : nullptr}
    , port_{params != nullptr ? params->port : 0} {}

void UdpManager::AddRef() {
    refCount_.fetch_add(1, std::memory_order_relaxed);
}

void UdpManager::Release() {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;
    }
}

void UdpManager::GiveTime() {
    // Stub implementation intentionally does nothing.
}

uint16_t UdpManager::GetPort() const {
    return port_;
}

