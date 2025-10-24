#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

using uchar = unsigned char;

class UdpConnection;

class UdpConnectionHandler {
public:
    virtual ~UdpConnectionHandler() = default;

    virtual void OnRoutePacket(UdpConnection* connection, const uchar* data, int length) {
        (void)connection;
        (void)data;
        (void)length;
    }
};

class UdpIpAddress {
public:
    UdpIpAddress();
    explicit UdpIpAddress(std::string address);

    const char* GetAddress(char* buffer) const;

    void SetAddress(std::string address);

private:
    std::string address_;
};

class UdpConnection {
public:
    enum Status {
        cStatusDisconnected,
        cStatusConnected
    };

    UdpConnection();
    UdpConnection(std::string destinationAddress, uint16_t destinationPort);

    void AddRef();
    void Release();

    void SetHandler(UdpConnectionHandler* handler);

    void Disconnect();

    void Send(uint8_t channel, const char* data, uint32_t length);

    const UdpIpAddress& GetDestinationIp() const;
    uint16_t GetDestinationPort() const;

    void SetDestination(std::string address, uint16_t port);

    Status GetStatus() const;
    void SetStatus(Status status);

private:
    std::atomic<int> refCount_;
    UdpConnectionHandler* handler_;
    UdpIpAddress destinationIp_;
    uint16_t destinationPort_;
    Status status_;
};

class UdpManagerHandler;

class UdpManager {
public:
    struct Params {
        UdpManagerHandler* handler{nullptr};
        uint16_t port{0};
        char bindIpAddress[64]{};
    };

    explicit UdpManager(const Params* params);

    void AddRef();
    void Release();

    void GiveTime();

    uint16_t GetPort() const;

private:
    std::atomic<int> refCount_;
    UdpManagerHandler* handler_;
    uint16_t port_;
};

class UdpManagerHandler {
public:
    virtual ~UdpManagerHandler() = default;

    virtual void OnConnectRequest(UdpConnection* connection) {
        (void)connection;
    }
};

constexpr uint8_t cUdpChannelReliable1 = 1;

