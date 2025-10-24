#include "ChatRoomService.hpp"

#include "ChatAvatarService.hpp"
#include "ChatEnums.hpp"
#include "FakeMariaDB.hpp"
#include "StringUtils.hpp"

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

TEST_CASE("CreateRoom prevents duplicates and persists UTF-8 metadata", "[stationchat]") {
    MariaDBConnection db;
    ChatAvatarService avatarService{&db};
    ChatRoomService service{&avatarService, &db};

    auto* creator = avatarService.CreateAvatar(u"Åsa", u"corellia", 101, 0, u"mos eisley");

    const std::u16string roomName = u"カフェ";
    const std::u16string roomTopic = u"☕️ break";
    const std::u16string roomAddress = u"galaxy";
    const uint32_t attributes = static_cast<uint32_t>(RoomAttributes::PERSISTENT);

    auto* room = service.CreateRoom(creator, roomName, roomTopic, u"", attributes, 64, roomAddress, creator->GetAddress());
    REQUIRE(room != nullptr);
    CHECK(room->IsPersistent());
    REQUIRE(db.insertedRooms.size() == 1);
    CHECK(db.insertedRooms.front().roomName == FromWideString(roomName));
    CHECK(db.insertedRooms.front().roomAddress == FromWideString(roomAddress));

    try {
        service.CreateRoom(creator, roomName, roomTopic, u"", attributes, 64, roomAddress, creator->GetAddress());
        FAIL("Expected duplicate room creation to throw");
    } catch (const ChatResultException& ex) {
        CHECK(ex.code == ChatResultCode::ROOM_ALREADYEXISTS);
    }
}

