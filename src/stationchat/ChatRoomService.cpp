#include "ChatRoomService.hpp"
#include "ChatAvatarService.hpp"
#include "ChatSystem.hpp"
#include "MariaDB.hpp"
#include "StreamUtils.hpp"
#include "StringUtils.hpp"

#include "easylogging++.h"

ChatRoomService::ChatRoomService(ChatAvatarService* avatarService, MariaDBConnection* db)
    : avatarService_{avatarService}
    , db_{db} {}

ChatRoomService::~ChatRoomService() {}

void ChatRoomService::LoadRoomsFromStorage(const std::u16string& baseAddress) {
    rooms_.clear();

    MariaDBStatement* stmt;

    char sql[] = "SELECT id, creator_id, creator_name, creator_address, room_name, room_topic, "
                 "room_password, room_prefix, room_address, room_attributes, room_max_size, "
                 "room_message_id, created_at, node_level FROM room WHERE room_address LIKE @baseAddress||'%'";

    if (mariadb_prepare(db_, sql, -1, &stmt, 0) != MARIADB_OK) {
        throw std::runtime_error("Error preparing SQL statement");
    }

    int baseAddressIdx = mariadb_bind_parameter_index(stmt, "@baseAddress");

    auto baseAddressStr = FromWideString(baseAddress);
    LOG(INFO) << "Loading rooms for base address: " << baseAddressStr;
    mariadb_bind_text(stmt, baseAddressIdx, baseAddressStr.c_str(), -1, 0);

    while (mariadb_step(stmt) == MARIADB_ROW) {
        auto room = std::make_unique<ChatRoom>();
        std::string tmp;
        room->roomId_ = nextRoomId_++;
        room->dbId_ = mariadb_column_int(stmt, 0);
        room->creatorId_ = mariadb_column_int(stmt, 1);

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 2)));
        room->creatorName_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 3)));
        room->creatorAddress_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 4)));
        room->roomName_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 5)));
        room->roomTopic_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 6)));
        room->roomPassword_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 7)));
        room->roomPrefix_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 8)));
        room->roomAddress_ = std::u16string{std::begin(tmp), std::end(tmp)};

        room->roomAttributes_ = mariadb_column_int(stmt, 9);
        room->maxRoomSize_ = mariadb_column_int(stmt, 10);
        room->roomMessageId_ = mariadb_column_int(stmt, 11);
        room->createTime_ = mariadb_column_int(stmt, 12);
        room->nodeLevel_ = mariadb_column_int(stmt, 13);

        room->SetChatSystem(DetermineChatSystem(room->GetRoomName(), room->GetRoomAddress()));

        if (!RoomExists(room->GetRoomAddress())) {
            rooms_.emplace_back(std::move(room));
        }
    }

    LOG(INFO) << "Rooms currently loaded: " << rooms_.size();
}

ChatRoom* ChatRoomService::CreateRoom(const ChatAvatar* creator,
    const std::u16string& roomName, const std::u16string& roomTopic, const std::u16string& roomPassword,
    uint32_t roomAttributes, uint32_t maxRoomSize, const std::u16string& roomAddress,
    const std::u16string& srcAddress) {
    ChatRoom* roomPtr = nullptr;

    if (RoomExists(roomAddress + u"+" + roomName)) {
        throw ChatResultException(ChatResultCode::ROOM_ALREADYEXISTS, "ChatRoom already exists");
    }

    LOG(INFO) << "Creating room " << FromWideString(roomName) << "@" << FromWideString(roomAddress) << " with attributes "
              << roomAttributes;

    rooms_.emplace_back(std::make_unique<ChatRoom>(this, nextRoomId_++, creator, roomName,
        roomTopic, roomPassword, roomAttributes, maxRoomSize, roomAddress, srcAddress));
    roomPtr = rooms_.back().get();

    roomPtr->SetChatSystem(DetermineChatSystem(roomPtr->GetRoomName(), roomPtr->GetRoomAddress()));

    if (roomPtr->IsPersistent()) {
        PersistNewRoom(*roomPtr);
    }

    return roomPtr;
}

void ChatRoomService::DestroyRoom(ChatRoom* room) {
    if (room->IsPersistent()) {
        DeleteRoom(room);
    }

    rooms_.erase(std::remove_if(std::begin(rooms_), std::end(rooms_),
        [room](const auto& trackedRoom) { return trackedRoom->GetRoomId() == room->GetRoomId(); }));
}

ChatResultCode ChatRoomService::PersistNewRoom(ChatRoom& room) {
    ChatResultCode result = ChatResultCode::SUCCESS;
    MariaDBStatement* stmt;

    char sql[] = "INSERT INTO room (creator_id, creator_name, creator_address, room_name, "
                 "room_topic, room_password, room_prefix, room_address, room_attributes, "
                 "room_max_size, room_message_id, created_at, node_level) VALUES (@creator_id, "
                 "@creator_name, @creator_address, @room_name, @room_topic, @room_password, "
                 "@room_prefix, @room_address, @room_attributes, @room_max_size, @room_message_id, "
                 "@created_at, @node_level)";

    if (mariadb_prepare(db_, sql, -1, &stmt, 0) != MARIADB_OK) {
        result = ChatResultCode::DBFAIL;
    } else {
        int creatorIdIdx = mariadb_bind_parameter_index(stmt, "@creator_id");
        int creatorNameIdx = mariadb_bind_parameter_index(stmt, "@creator_name");
        int creatorAddressIdx = mariadb_bind_parameter_index(stmt, "@creator_address");
        int roomNameIdx = mariadb_bind_parameter_index(stmt, "@room_name");
        int roomTopicIdx = mariadb_bind_parameter_index(stmt, "@room_topic");
        int roomPasswordIdx = mariadb_bind_parameter_index(stmt, "@room_password");
        int roomPrefixIdx = mariadb_bind_parameter_index(stmt, "@room_prefix");
        int roomAddressIdx = mariadb_bind_parameter_index(stmt, "@room_address");
        int roomAttributesIdx = mariadb_bind_parameter_index(stmt, "@room_attributes");
        int roomMaxSizeIdx = mariadb_bind_parameter_index(stmt, "@room_max_size");
        int roomMessageIdIdx = mariadb_bind_parameter_index(stmt, "@room_message_id");
        int createdAtIdx = mariadb_bind_parameter_index(stmt, "@created_at");
        int nodeLevelIdx = mariadb_bind_parameter_index(stmt, "@node_level");

        mariadb_bind_int(stmt, creatorIdIdx, room.creatorId_);

        auto creatorName = FromWideString(room.creatorName_);
        mariadb_bind_text(stmt, creatorNameIdx, creatorName.c_str(), -1, 0);

        auto creatorAddress = FromWideString(room.creatorAddress_);
        mariadb_bind_text(stmt, creatorAddressIdx, creatorAddress.c_str(), -1, 0);

        auto roomName = FromWideString(room.roomName_);
        mariadb_bind_text(stmt, roomNameIdx, roomName.c_str(), -1, 0);

        auto roomTopic = FromWideString(room.roomTopic_);
        mariadb_bind_text(stmt, roomTopicIdx, roomTopic.c_str(), -1, 0);

        auto roomPassword = FromWideString(room.roomPassword_);
        mariadb_bind_text(stmt, roomPasswordIdx, roomPassword.c_str(), -1, 0);

        auto roomPrefix = FromWideString(room.roomPrefix_);
        mariadb_bind_text(stmt, roomPrefixIdx, roomPrefix.c_str(), -1, 0);

        auto roomAddress = FromWideString(room.roomAddress_);
        mariadb_bind_text(stmt, roomAddressIdx, roomAddress.c_str(), -1, 0);

        mariadb_bind_int(stmt, roomAttributesIdx, room.roomAttributes_);
        mariadb_bind_int(stmt, roomMaxSizeIdx, room.maxRoomSize_);
        mariadb_bind_int(stmt, roomMessageIdIdx, room.roomMessageId_);
        mariadb_bind_int(stmt, createdAtIdx, room.createTime_);
        mariadb_bind_int(stmt, nodeLevelIdx, room.nodeLevel_);

        if (mariadb_step(stmt) != MARIADB_DONE) {
            result = ChatResultCode::DBFAIL;
        } else {
            room.dbId_ = static_cast<uint32_t>(mariadb_last_insert_rowid(db_));
        }
    }

    return result;
}

std::vector<ChatRoom*> ChatRoomService::GetRoomSummaries(
    const std::u16string& startNode, const std::u16string& filter) {
    std::vector<ChatRoom*> rooms;

    for (auto& room : rooms_) {
        auto& roomAddress = room->GetRoomAddress();
        if (roomAddress.compare(0, startNode.length(), startNode) == 0) {
            if (!room->IsPrivate()) {
                rooms.push_back(room.get());
            }
        }
    }

    return rooms;
}

bool ChatRoomService::RoomExists(const std::u16string& roomAddress) const {
    return std::find_if(std::begin(rooms_), std::end(rooms_), [roomAddress](auto& room) {
        return roomAddress.compare(room->GetRoomAddress()) == 0;
    }) != std::end(rooms_);
}

ChatRoom* ChatRoomService::GetRoom(const std::u16string& roomAddress) {
    ChatRoom* room = nullptr;

    auto find_iter = std::find_if(std::begin(rooms_), std::end(rooms_),
        [roomAddress](auto& room) { return roomAddress.compare(room->GetRoomAddress()) == 0; });

    if (find_iter != std::end(rooms_)) {
        room = find_iter->get();
    }

    return room;
}

std::vector<ChatRoom*> ChatRoomService::GetJoinedRooms(const ChatAvatar * avatar) {
    std::vector<ChatRoom*> rooms;

    for (auto& room : rooms_) {
        if (room->IsInRoom(avatar->GetAvatarId())) {
            rooms.push_back(room.get());
        }
    }

    return rooms;
}

void ChatRoomService::DeleteRoom(ChatRoom* room) {
    MariaDBStatement* stmt;
    char sql[] = "DELETE FROM room WHERE id = @id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int idIdx = mariadb_bind_parameter_index(stmt, "@id");
    mariadb_bind_int(stmt, idIdx, room->dbId_);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatRoomService::LoadModerators(ChatRoom * room) {
    MariaDBStatement* stmt;
    char sql[] = "SELECT moderator_avatar_id FROM room_moderator WHERE room_id = @room_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");
    mariadb_bind_int(stmt, roomIdIdx, room->GetRoomId());

    while (mariadb_step(stmt) == MARIADB_ROW) {
        uint32_t moderatorId = mariadb_column_int(stmt, 0);
        room->moderators_.push_back(avatarService_->GetAvatar(moderatorId));
    }
}

void ChatRoomService::PersistModerator(uint32_t moderatorId, uint32_t roomId) {
    MariaDBStatement* stmt;
    char sql[] = "INSERT OR IGNORE INTO room_moderator (moderator_avatar_id, room_id) VALUES (@moderator_avatar_id, @room_id)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int moderatorAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@moderator_avatar_id");
    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");

    mariadb_bind_int(stmt, moderatorAvatarIdIdx, moderatorId);
    mariadb_bind_int(stmt, roomIdIdx, roomId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatRoomService::DeleteModerator(uint32_t moderatorId, uint32_t roomId) {
    MariaDBStatement* stmt;
    char sql[] = "DELETE FROM room_moderator WHERE moderator_avatar_id = @moderator_avatar_id AND room_id = @room_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int moderatorAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@moderator_avatar_id");
    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");

    mariadb_bind_int(stmt, moderatorAvatarIdIdx, moderatorId);
    mariadb_bind_int(stmt, roomIdIdx, roomId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatRoomService::LoadAdministrators(ChatRoom * room) {
    MariaDBStatement* stmt;
    char sql[] = "SELECT administrator_avatar_id FROM room_administrator WHERE room_id = @room_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");
    mariadb_bind_int(stmt, roomIdIdx, room->GetRoomId());

    while (mariadb_step(stmt) == MARIADB_ROW) {
        uint32_t administratorId = mariadb_column_int(stmt, 0);
        room->administrators_.push_back(avatarService_->GetAvatar(administratorId));
    }
}

void ChatRoomService::PersistAdministrator(uint32_t administratorId, uint32_t roomId) {
    MariaDBStatement* stmt;
    char sql[] = "INSERT OR IGNORE INTO room_administrator (administrator_avatar_id, room_id) VALUES (@administrator_avatar_id, @room_id)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int administratorAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@administrator_avatar_id");
    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");

    mariadb_bind_int(stmt, administratorAvatarIdIdx, administratorId);
    mariadb_bind_int(stmt, roomIdIdx, roomId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatRoomService::DeleteAdministrator(uint32_t administratorId, uint32_t roomId) {
    MariaDBStatement* stmt;
    char sql[] = "DELETE FROM room_administrator WHERE administrator_avatar_id = @administrator_avatar_id AND room_id = @room_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int administratorAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@administrator_avatar_id");
    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");

    mariadb_bind_int(stmt, administratorAvatarIdIdx, administratorId);
    mariadb_bind_int(stmt, roomIdIdx, roomId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatRoomService::LoadBanned(ChatRoom * room) {
    MariaDBStatement* stmt;
    char sql[] = "SELECT banned_avatar_id FROM room_ban WHERE room_id = @room_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");
    mariadb_bind_int(stmt, roomIdIdx, room->GetRoomId());

    while (mariadb_step(stmt) == MARIADB_ROW) {
        uint32_t bannedId = mariadb_column_int(stmt, 0);
        room->banned_.push_back(avatarService_->GetAvatar(bannedId));
    }
}

void ChatRoomService::PersistBanned(uint32_t bannedId, uint32_t roomId) {
    MariaDBStatement* stmt;
    char sql[] = "INSERT OR IGNORE INTO room_ban (banned_avatar_id, room_id) VALUES (@banned_avatar_id, @room_id)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int bannedAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@moderator_avatar_id");
    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");

    mariadb_bind_int(stmt, bannedAvatarIdIdx, bannedId);
    mariadb_bind_int(stmt, roomIdIdx, roomId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatRoomService::DeleteBanned(uint32_t bannedId, uint32_t roomId) {
    MariaDBStatement* stmt;
    char sql[] = "DELETE FROM room_ban WHERE banned_avatar_id = @banned_avatar_id AND room_id = @room_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int bannedAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@banned_avatar_id");
    int roomIdIdx = mariadb_bind_parameter_index(stmt, "@room_id");

    mariadb_bind_int(stmt, bannedAvatarIdIdx, bannedId);
    mariadb_bind_int(stmt, roomIdIdx, roomId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}
