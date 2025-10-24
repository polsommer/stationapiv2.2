#include "stationchat/ChatRoomService.hpp"

#include "FakeMariaDB.hpp"

#include "catch.hpp"

#include <string>

namespace {

FakeRoomRow MakeRoomRow(int id, const std::string& roomAddress, const std::string& roomName) {
    FakeRoomRow row;
    row.id = id;
    row.creatorId = id * 10;
    row.creatorName = "creator" + std::to_string(id);
    row.creatorAddress = "addr" + std::to_string(id);
    row.roomName = roomName;
    row.roomTopic = "topic" + std::to_string(id);
    row.roomPassword = "";
    row.roomPrefix = "";
    row.roomAddress = roomAddress;
    row.roomAttributes = 0;
    row.roomMaxSize = 50;
    row.roomMessageId = 0;
    row.createdAt = 0;
    row.nodeLevel = 0;
    return row;
}

}

TEST_CASE("LoadRoomsFromStorage filters by base address prefix", "[stationchat]") {
    MariaDBConnection db;
    db.roomRows.push_back(MakeRoomRow(1, "base+alpha", "alpha"));
    db.roomRows.push_back(MakeRoomRow(2, "base+beta", "beta"));
    db.roomRows.push_back(MakeRoomRow(3, "other+gamma", "gamma"));

    ChatRoomService service{nullptr, &db};

    service.LoadRoomsFromStorage(u"base");

    REQUIRE(db.lastPreparedSql.find("LIKE CONCAT(@baseAddress, '%')") != std::string::npos);

    auto* alpha = service.GetRoom(u"base+alpha");
    auto* beta = service.GetRoom(u"base+beta");
    auto* gamma = service.GetRoom(u"other+gamma");

    REQUIRE(alpha != nullptr);
    REQUIRE(beta != nullptr);
    REQUIRE(gamma == nullptr);

    CHECK(alpha->GetRoomName() == std::u16string{u"alpha"});
    CHECK(beta->GetRoomName() == std::u16string{u"beta"});
}

