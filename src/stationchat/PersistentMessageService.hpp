
#pragma once

#include "ChatEnums.hpp"
#include "PersistentMessage.hpp"

#include <boost/optional.hpp>

#include <cstdint>
#include <vector>

struct MariaDBConnection;

class PersistentMessageService {
public:
    explicit PersistentMessageService(MariaDBConnection* db);
    ~PersistentMessageService();

    void StoreMessage(PersistentMessage& message);

    std::vector<PersistentHeader> GetMessageHeaders(uint32_t avatarId);

    PersistentMessage GetPersistentMessage(uint32_t avatarId, uint32_t messageId);

    void UpdateMessageStatus(
        uint32_t avatarId, uint32_t messageId, PersistentState status);

    void BulkUpdateMessageStatus(
        uint32_t avatarId, const std::u16string& category, PersistentState newStatus);

private:
    MariaDBConnection* db_;
};
