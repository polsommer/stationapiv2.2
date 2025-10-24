
#pragma once

#include "UdpLibrary.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <stdexcept>

template <typename NodeT, typename ClientT>
class Node : public UdpManagerHandler
{
private:
    struct UdpManagerDeleter
    {
        void operator()(UdpManager *manager) const noexcept
        {
            if (manager != nullptr)
            {
                manager->Release();
            }
        }
    };

    using UdpManagerPtr = std::unique_ptr<UdpManager, UdpManagerDeleter>;

public:
    explicit Node(NodeT *node, const std::string &listenAddress, uint16_t listenPort, bool bindToIp = false)
        : node_{node}
    {

        UdpManager::Params params;
        params.handler = this;
        params.port = listenPort;

        if (bindToIp)
        {
            if (listenAddress.length() > sizeof(params.bindIpAddress))
            {
                throw std::runtime_error{"Invalid bind ip specified: " + listenAddress};
            }

            std::copy(std::begin(listenAddress), std::end(listenAddress), params.bindIpAddress);
        }

        UdpManagerPtr udpManager{new UdpManager(&params)};
        udpManager_ = std::move(udpManager);
    }

    void Tick()
    {
        udpManager_->GiveTime();

        auto remove_iter = std::remove_if(std::begin(clients_), std::end(clients_), [](auto &client)
                                          { return client->GetConnection()->GetStatus() == UdpConnection::cStatusDisconnected; });

        if (remove_iter != std::end(clients_))
            clients_.erase(remove_iter, clients_.end());

        OnTick();
    }

private:
    virtual void OnTick() = 0;

    void OnConnectRequest(UdpConnection *connection) override
    {
        AddClient(std::make_unique<ClientT>(connection, node_));
    }

    void AddClient(std::unique_ptr<ClientT> client) { clients_.push_back(std::move(client)); }

    std::vector<std::unique_ptr<ClientT>> clients_;
    NodeT *node_;
    UdpManagerPtr udpManager_;
};
