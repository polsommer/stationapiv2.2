#include "ChatAvatarService.hpp"
#include "ChatAvatar.hpp"
#include "MariaDB.hpp"
#include "StringUtils.hpp"

#include <easylogging++.h>

ChatAvatarService::ChatAvatarService(MariaDBConnection* db)
    : db_{db} {}

ChatAvatarService::~ChatAvatarService() {}

ChatAvatar* ChatAvatarService::GetAvatar(const std::u16string& name, const std::u16string& address) {
    ChatAvatar* avatar = GetCachedAvatar(name, address);

    if (!avatar) {
        auto loadedAvatar = LoadStoredAvatar(name, address);
        if (loadedAvatar != nullptr) {
            avatar = loadedAvatar.get();
            avatarCache_.emplace_back(std::move(loadedAvatar));

            LoadFriendList(avatar);
            LoadIgnoreList(avatar);
        }
    }

    return avatar;
}

ChatAvatar* ChatAvatarService::GetAvatar(uint32_t avatarId) {
    ChatAvatar* avatar = GetCachedAvatar(avatarId);

    if (!avatar) {
        auto loadedAvatar = LoadStoredAvatar(avatarId);
        if (loadedAvatar != nullptr) {
            avatar = loadedAvatar.get();
            avatarCache_.emplace_back(std::move(loadedAvatar));

            LoadFriendList(avatar);
            LoadIgnoreList(avatar);
        }
    }

    return avatar;
}

ChatAvatar* ChatAvatarService::CreateAvatar(const std::u16string& name, const std::u16string& address,
    uint32_t userId, uint32_t loginAttributes, const std::u16string& loginLocation) {
    auto tmp
        = std::make_unique<ChatAvatar>(this, name, address, userId, loginAttributes, loginLocation);
    auto avatar = tmp.get();

    InsertAvatar(avatar);

    avatarCache_.emplace_back(std::move(tmp));

    return avatar;
}

void ChatAvatarService::DestroyAvatar(ChatAvatar* avatar) {
    DeleteAvatar(avatar);
    LogoutAvatar(avatar);
    RemoveCachedAvatar(avatar->GetAvatarId());
}

void ChatAvatarService::LoginAvatar(ChatAvatar* avatar) {
    avatar->isOnline_ = true;

    if (!IsOnline(avatar)) {
        onlineAvatars_.push_back(avatar);
    }
}

void ChatAvatarService::LogoutAvatar(ChatAvatar* avatar) {
	if(!avatar->isOnline_) return;
    avatar->isOnline_ = false;

    onlineAvatars_.erase(std::remove_if(
        std::begin(onlineAvatars_), std::end(onlineAvatars_), [avatar](auto onlineAvatar) {
            return onlineAvatar->GetAvatarId() == avatar->GetAvatarId();
        }));
}

void ChatAvatarService::PersistAvatar(const ChatAvatar* avatar) { UpdateAvatar(avatar); }

void ChatAvatarService::PersistFriend(
    uint32_t srcAvatarId, uint32_t destAvatarId, const std::u16string& comment) {
    MariaDBStatement* stmt;
    char sql[] = "INSERT INTO friend (avatar_id, friend_avatar_id, comment) VALUES (@avatar_id, "
                 "@friend_avatar_id, @comment)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int friendAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@friend_avatar_id");
    int commentIdx = mariadb_bind_parameter_index(stmt, "@comment");

    std::string commentStr = FromWideString(comment);

    mariadb_bind_int(stmt, avatarIdIdx, srcAvatarId);
    mariadb_bind_int(stmt, friendAvatarIdIdx, destAvatarId);
    mariadb_bind_text(stmt, commentIdx, commentStr.c_str(), -1, 0);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatAvatarService::PersistIgnore(uint32_t srcAvatarId, uint32_t destAvatarId) {
    MariaDBStatement* stmt;
    char sql[] = "INSERT INTO ignore (avatar_id, ignore_avatar_id, comment) VALUES (@avatar_id, "
                 "@ignore_avatar_id)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int ignoreAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@ignore_avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, srcAvatarId);
    mariadb_bind_int(stmt, ignoreAvatarIdIdx, destAvatarId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatAvatarService::RemoveFriend(uint32_t srcAvatarId, uint32_t destAvatarId) {
    MariaDBStatement* stmt;

    char sql[] = "DELETE FROM friend WHERE avatar_id = @avatar_id AND friend_avatar_id = "
                 "@friend_avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int friendAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@friend_avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, srcAvatarId);
    mariadb_bind_int(stmt, friendAvatarIdIdx, destAvatarId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatAvatarService::RemoveIgnore(uint32_t srcAvatarId, uint32_t destAvatarId) {
    MariaDBStatement* stmt;

    char sql[] = "DELETE FROM ignore WHERE avatar_id = @avatar_id AND ignore_avatar_id = "
                 "@ignore_avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int ignoreAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@ignore_avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, srcAvatarId);
    mariadb_bind_int(stmt, ignoreAvatarIdIdx, destAvatarId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

void ChatAvatarService::UpdateFriendComment(
    uint32_t srcAvatarId, uint32_t destAvatarId, const std::u16string& comment) {
    MariaDBStatement* stmt;
    char sql[] = "UDPATE friend SET comment = @comment WHERE avatar_id = @avatar_id AND "
                 "friend_avatar_id = @friend_avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int commentIdx = mariadb_bind_parameter_index(stmt, "@comment");
    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int friendAvatarIdIdx = mariadb_bind_parameter_index(stmt, "@friend_avatar_id");

    std::string commentStr = FromWideString(comment);

    mariadb_bind_text(stmt, commentIdx, commentStr.c_str(), -1, 0);
    mariadb_bind_int(stmt, avatarIdIdx, srcAvatarId);
    mariadb_bind_int(stmt, friendAvatarIdIdx, destAvatarId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
}

ChatAvatar* ChatAvatarService::GetCachedAvatar(
    const std::u16string& name, const std::u16string& address) {
    ChatAvatar* avatar = nullptr;

    // First look for the avatar in the cache
    auto find_iter = std::find_if(
        std::begin(avatarCache_), std::end(avatarCache_), [name, address](const auto& avatar) {
            return avatar->name_.compare(name) == 0 && avatar->address_.compare(address) == 0;
        });

    if (find_iter != std::end(avatarCache_)) {
        avatar = find_iter->get();
    }

    return avatar;
}

ChatAvatar* ChatAvatarService::GetCachedAvatar(uint32_t avatarId) {
    ChatAvatar* avatar = nullptr;

    // First look for the avatar in the cache
    auto find_iter = std::find_if(std::begin(avatarCache_), std::end(avatarCache_),
        [avatarId](const auto& avatar) { return avatar->avatarId_ == avatarId; });

    if (find_iter != std::end(avatarCache_)) {
        avatar = find_iter->get();
    }

    return avatar;
}

void ChatAvatarService::RemoveCachedAvatar(uint32_t avatarId) {
    auto remove_iter = std::remove_if(std::begin(avatarCache_), std::end(avatarCache_),
                                  [avatarId](const auto& avatar) { return avatar->avatarId_ == avatarId; });

    if (remove_iter != std::end(avatarCache_)) {
        avatarCache_.erase(remove_iter);
    }
}

void ChatAvatarService::RemoveAsFriendOrIgnoreFromAll(const ChatAvatar* avatar) {
    for (auto& cachedAvatar : avatarCache_) {
        if (cachedAvatar->IsFriend(avatar)) {
            cachedAvatar->RemoveFriend(avatar);
        }

        if (cachedAvatar->IsIgnored(avatar)) {
            cachedAvatar->RemoveIgnore(avatar);
        }
    }
}

std::unique_ptr<ChatAvatar> ChatAvatarService::LoadStoredAvatar(
    const std::u16string& name, const std::u16string& address) {
    std::unique_ptr<ChatAvatar> avatar{nullptr};

    MariaDBStatement* stmt;

    char sql[] = "SELECT id, user_id, name, address, attributes FROM avatar WHERE name = @name AND "
                 "address = @address";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    std::string nameStr = FromWideString(name);
    std::string addressStr = FromWideString(address);

    int nameIdx = mariadb_bind_parameter_index(stmt, "@name");
    int addressIdx = mariadb_bind_parameter_index(stmt, "@address");

    mariadb_bind_text(stmt, nameIdx, nameStr.c_str(), -1, 0);
    mariadb_bind_text(stmt, addressIdx, addressStr.c_str(), -1, 0);

    if (mariadb_step(stmt) == MARIADB_ROW) {
        avatar = std::make_unique<ChatAvatar>(this);
        avatar->avatarId_ = mariadb_column_int(stmt, 0);
        avatar->userId_ = mariadb_column_int(stmt, 1);

        auto tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 2)));
        avatar->name_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 3)));
        avatar->address_ = std::u16string(std::begin(tmp), std::end(tmp));

        avatar->attributes_ = mariadb_column_int(stmt, 4);
    }

    mariadb_finalize(stmt);

    return avatar;
}

std::unique_ptr<ChatAvatar> ChatAvatarService::LoadStoredAvatar(uint32_t avatarId) {
    std::unique_ptr<ChatAvatar> avatar{nullptr};

    MariaDBStatement* stmt;

    char sql[] = "SELECT id, user_id, name, address, attributes FROM avatar WHERE id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, avatarId);

    if (mariadb_step(stmt) == MARIADB_ROW) {
        avatar = std::make_unique<ChatAvatar>(this);
        avatar->avatarId_ = mariadb_column_int(stmt, 0);
        avatar->userId_ = mariadb_column_int(stmt, 1);

        auto tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 2)));
        avatar->name_ = std::u16string{std::begin(tmp), std::end(tmp)};

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 3)));
        avatar->address_ = std::u16string(std::begin(tmp), std::end(tmp));

        avatar->attributes_ = mariadb_column_int(stmt, 4);
    }

    mariadb_finalize(stmt);

    return avatar;
}

void ChatAvatarService::InsertAvatar(ChatAvatar* avatar) {
    CHECK_NOTNULL(avatar);
    MariaDBStatement* stmt;

    char sql[] = "INSERT INTO avatar (user_id, name, address, attributes) VALUES (@user_id, @name, "
                 "@address, @attributes)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    std::string nameStr = FromWideString(avatar->name_);
    std::string addressStr = FromWideString(avatar->address_);

    int userIdIdx = mariadb_bind_parameter_index(stmt, "@user_id");
    int nameIdx = mariadb_bind_parameter_index(stmt, "@name");
    int addressIdx = mariadb_bind_parameter_index(stmt, "@address");
    int attributesIdx = mariadb_bind_parameter_index(stmt, "@attributes");

    mariadb_bind_int(stmt, userIdIdx, avatar->userId_);
    mariadb_bind_text(stmt, nameIdx, nameStr.c_str(), -1, 0);
    mariadb_bind_text(stmt, addressIdx, addressStr.c_str(), -1, 0);
    mariadb_bind_int(stmt, attributesIdx, avatar->attributes_);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    avatar->avatarId_ = static_cast<uint32_t>(mariadb_last_insert_rowid(db_));

    mariadb_finalize(stmt);
}

void ChatAvatarService::UpdateAvatar(const ChatAvatar* avatar) {
    CHECK_NOTNULL(avatar);
    MariaDBStatement* stmt;

    char sql[] = "UPDATE avatar SET user_id = @user_id, name = @name, address = @address, "
                 "attributes = @attributes "
                 "WHERE id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    std::string nameStr = FromWideString(avatar->name_);
    std::string addressStr = FromWideString(avatar->address_);

    int userIdIdx = mariadb_bind_parameter_index(stmt, "@user_id");
    int nameIdx = mariadb_bind_parameter_index(stmt, "@name");
    int addressIdx = mariadb_bind_parameter_index(stmt, "@address");
    int attributesIdx = mariadb_bind_parameter_index(stmt, "@attributes");
    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, userIdIdx, avatar->userId_);
    mariadb_bind_text(stmt, nameIdx, nameStr.c_str(), -1, 0);
    mariadb_bind_text(stmt, addressIdx, addressStr.c_str(), -1, 0);
    mariadb_bind_int(stmt, attributesIdx, avatar->attributes_);
    mariadb_bind_int(stmt, avatarIdIdx, avatar->avatarId_);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
}

void ChatAvatarService::DeleteAvatar(ChatAvatar* avatar) {
    CHECK_NOTNULL(avatar);
    MariaDBStatement* stmt;

    char sql[] = "DELETE FROM avatar WHERE id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, avatar->avatarId_);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
}

void ChatAvatarService::LoadFriendList(ChatAvatar* avatar) {
    MariaDBStatement* stmt;

    char sql[] = "SELECT friend_avatar_id, comment FROM friend WHERE avatar_id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, avatar->avatarId_);

    uint32_t tmpFriendId;
    std::string tmpComment;
    while (mariadb_step(stmt) == MARIADB_ROW) {
        tmpFriendId = mariadb_column_int(stmt, 0);
        tmpComment = reinterpret_cast<const char*>(mariadb_column_text(stmt, 1));

        auto friendAvatar = GetAvatar(tmpFriendId);

        avatar->friendList_.emplace_back(friendAvatar, ToWideString(tmpComment));
    }
}

void ChatAvatarService::LoadIgnoreList(ChatAvatar* avatar) {
    MariaDBStatement* stmt;

    char sql[] = "SELECT ignore_avatar_id FROM ignore WHERE avatar_id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, avatar->avatarId_);

    uint32_t tmpIgnoreId;
    while (mariadb_step(stmt) == MARIADB_ROW) {
        tmpIgnoreId = mariadb_column_int(stmt, 0);

        auto ignoreAvatar = GetAvatar(tmpIgnoreId);

        avatar->ignoreList_.emplace_back(ignoreAvatar);
    }
}

bool ChatAvatarService::IsOnline(const ChatAvatar * avatar) const {
    for (auto onlineAvatar : onlineAvatars_) {
        if (onlineAvatar->GetAvatarId() == avatar->GetAvatarId()) {
            return true;
        }
    }

    return false;
}
