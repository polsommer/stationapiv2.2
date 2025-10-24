#pragma once

#include "stationapi/MariaDB.hpp"

#include <optional>
#include <string>
#include <unordered_map>
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

struct FakeColumnDefinition {
    std::string typeName;
    bool isDateTime{false};
};

struct FakeTimestampValue {
    bool isNull{true};
    bool isDateTime{false};
    std::string textValue;
    uint32_t intValue{0};
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
    bool isOnline{false};
    FakeTimestampValue lastLogin;
    FakeTimestampValue lastLogout;
    FakeTimestampValue updatedAt;
    FakeTimestampValue createdAt;
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
    std::string oob;
    uint32_t sentTime{0};
    FakeTimestampValue createdAt;
    FakeTimestampValue updatedAt;
    uint32_t status{0};
};

struct MariaDBConnection {
    std::vector<FakeRoomRow> roomRows;
    std::string lastError{"OK"};
    std::string lastPreparedSql;
    std::string userLinkTableName;
    std::string statusTableName;
    std::string mailTableName;
    std::unordered_map<std::string, std::unordered_map<std::string, FakeColumnDefinition>> tableColumns;
    std::unordered_map<std::string, std::vector<FakeUserLinkRow>> userLinkRows;
    std::unordered_map<std::string, std::vector<FakeStatusRow>> statusRows;
    std::unordered_map<std::string, std::vector<FakeMailRow>> mailRows;
    std::unordered_map<std::string, int> preparedStatementCount;
};

