#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

using uchar = unsigned char;

constexpr int cUdpChannelReliable1 = 0;

class UdpConnection;

class UdpConnectionHandler {
public:
    virtual ~UdpConnectionHandler() = default;
    virtual void OnRoutePacket(UdpConnection* connection, const uchar* data, int length) = 0;
};

class UdpManagerHandler {
public:
    virtual ~UdpManagerHandler() = default;
    virtual void OnConnectRequest(UdpConnection* connection) = 0;
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
    enum Status { cStatusConnected, cStatusDisconnected };

    UdpConnection();

    void AddRef();
    void Release();

    void SetHandler(UdpConnectionHandler* handler);
    void Disconnect();
    void Send(int channel, const char* data, uint32_t length);

    Status GetStatus() const;
    const UdpIpAddress& GetDestinationIp() const;
    uint16_t GetDestinationPort() const;

    void SetDestination(const std::string& address, uint16_t port);
    void SimulateIncoming(const uchar* data, int length);

private:
    std::atomic<int> refCount_;
    Status status_;
    UdpConnectionHandler* handler_;
    UdpIpAddress destination_;
    uint16_t destinationPort_;
};

class UdpManager {
public:
    struct Params {
        UdpManagerHandler* handler = nullptr;
        uint16_t port = 0;
        char bindIpAddress[256] = {0};
    };

    explicit UdpManager(const Params* params);
    ~UdpManager();

    void Release();
    void GiveTime();

    UdpConnection* CreateConnection();

private:
    UdpManagerHandler* handler_;
    uint16_t listenPort_;
};

