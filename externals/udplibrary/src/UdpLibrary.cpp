#include "UdpLibrary.hpp"

#include <algorithm>
#include <utility>

namespace {
constexpr std::size_t UDP_ADDRESS_BUFFER = 256;
}

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
    , destinationPort_{0} {}

void UdpConnection::AddRef() { ++refCount_; }

void UdpConnection::Release() {
    if (--refCount_ == 0) {
        delete this;
    }
}

void UdpConnection::SetHandler(UdpConnectionHandler* handler) { handler_ = handler; }

void UdpConnection::Disconnect() { status_ = cStatusDisconnected; }

void UdpConnection::Send(int, const char*, uint32_t) {
    // The open-source stub does not implement real networking. This method is
    // intentionally a no-op so that the rest of the application can be tested
    // without the proprietary dependency.
}

UdpConnection::Status UdpConnection::GetStatus() const { return status_; }

const UdpIpAddress& UdpConnection::GetDestinationIp() const { return destination_; }

uint16_t UdpConnection::GetDestinationPort() const { return destinationPort_; }

void UdpConnection::SetDestination(const std::string& address, uint16_t port) {
    destination_.SetAddress(address);
    destinationPort_ = port;
}

void UdpConnection::SimulateIncoming(const uchar* data, int length) {
    if (handler_ == nullptr || data == nullptr || length <= 0 || status_ != cStatusConnected) {
        return;
    }

    handler_->OnRoutePacket(this, data, length);
}

UdpManager::UdpManager(const Params* params)
    : handler_{params != nullptr ? params->handler : nullptr}
    , listenPort_{params != nullptr ? params->port : 0} {}

UdpManager::~UdpManager() = default;

void UdpManager::Release() { delete this; }

void UdpManager::GiveTime() {
    // Nothing to do in the stub implementation.
}

UdpConnection* UdpManager::CreateConnection() {
    auto* connection = new UdpConnection();
    connection->SetDestination("127.0.0.1", listenPort_);

    if (handler_ != nullptr) {
        handler_->OnConnectRequest(connection);
    }

    return connection;
}

