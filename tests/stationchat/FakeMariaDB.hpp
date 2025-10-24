#pragma once

#include "MariaDB.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct FakeRoomRow {
    int id{0};
    int creatorId{0};
    std::string creatorName;
    std::string creatorAddress;
    std::string roomName;
    std::string roomTopic;
    std::string roomPassword;
    std::string roomPrefix;
    std::string roomAddress;
    int roomAttributes{0};
    int roomMaxSize{0};
    int roomMessageId{0};
    int createdAt{0};
    int nodeLevel{0};
};

struct FakePersistentMessageRow {
    uint32_t id{0};
    uint32_t avatarId{0};
    std::string fromName;
    std::string fromAddress;
    std::string subject;
    uint32_t sentTime{0};
    uint32_t status{0};
    std::string folder;
    std::string category;
    std::string message;
    std::vector<uint8_t> oob;
};

struct FakeTimestampValue {
    bool isNull{true};
    std::optional<int> intValue;
    std::optional<std::string> textValue;
};

struct FakeUserLinkRow {
    uint32_t userId{0};
    uint32_t avatarId{0};
    std::string avatarName;
    FakeTimestampValue createdAt;
    FakeTimestampValue updatedAt;
};

struct FakeStatusRow {
    uint32_t avatarId{0};
    uint32_t userId{0};
    std::string avatarName;
    uint32_t isOnline{0};
    uint32_t lastLogin{0};
    uint32_t lastLogout{0};
    FakeTimestampValue createdAt;
    FakeTimestampValue updatedAt;
};

struct FakeMailRow {
    uint32_t avatarId{0};
    uint32_t userId{0};
    std::string avatarName;
    uint32_t messageId{0};
    std::string senderName;
    std::string senderAddress;
    std::string subject;
    std::string body;
    std::string oobText;
    uint32_t sentTime{0};
    FakeTimestampValue createdAt;
    FakeTimestampValue updatedAt;
    uint32_t status{0};
};

struct FakeColumnDefinition {
    bool exists{false};
    bool isDateTime{false};
    std::string type;
};

struct MariaDBConnection {
    std::vector<FakeRoomRow> roomRows;
    std::vector<FakeRoomRow> insertedRooms;
    std::vector<FakePersistentMessageRow> persistentMessages;
    std::vector<FakeUserLinkRow> websiteUserLinks;
    std::vector<FakeStatusRow> websiteStatusRows;
    std::vector<FakeMailRow> websiteMailRows;
    std::map<std::string, std::map<std::string, FakeColumnDefinition>> columnDefinitions;
    std::vector<std::string> preparedSql;
    std::string lastError{"OK"};
    std::string lastPreparedSql;
    std::string userLinkTableName{"web_user_avatar"};
    std::string onlineStatusTableName{"web_avatar_status"};
    std::string mailTableName{"web_persistent_message"};
    std::int64_t lastInsertId{0};
    std::int64_t nextInsertId{1};
};

