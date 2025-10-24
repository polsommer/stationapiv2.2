#pragma once

#include "MariaDB.hpp"

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

struct MariaDBConnection {
    std::vector<FakeRoomRow> roomRows;
    std::string lastError{"OK"};
    std::string lastPreparedSql;
};

