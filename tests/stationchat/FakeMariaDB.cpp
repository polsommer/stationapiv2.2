#include "FakeMariaDB.hpp"

#include "PersistentMessage.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace {

enum class StatementType {
    Unknown,
    SelectRooms,
    InsertRoom,
    InsertPersistentMessage,
    SelectPersistentHeaders,
    SelectPersistentMessage,
    UpdatePersistentStatus,
    BulkUpdatePersistentStatus,
    WebsiteUserLink,
    WebsiteOnlineStatus,
    WebsiteMail,
    ShowColumns,
};

struct BoundValue {
    enum class Kind { None, Int, Text, Blob };

    Kind kind{Kind::None};
    int intValue{0};
    std::optional<std::string> textValue;
    std::vector<uint8_t> blobValue;
    bool isNull{false};
};

struct MariaDBStatement {
    MariaDBConnection* connection{nullptr};
    std::string sql;
    StatementType type{StatementType::Unknown};
    std::string tableName;
    bool prepared{false};
    std::size_t currentIndex{0};
    std::vector<const FakeRoomRow*> matchedRooms;
    std::vector<const FakePersistentMessageRow*> matchedMessages;
    const FakeRoomRow* currentRoom{nullptr};
    const FakePersistentMessageRow* currentMessage{nullptr};
    const FakeColumnDefinition* columnDefinition{nullptr};
    std::string columnName;
    bool returnedColumn{false};
    std::unordered_map<std::string, int> parameterIndexes;
    std::unordered_map<int, std::string> indexToName;
    std::unordered_map<std::string, BoundValue> boundValues;
};

std::string NormalizeParameter(const char* name) {
    if (!name) {
        return {};
    }

    std::string normalized{name};
    if (!normalized.empty() && normalized.front() == '@') {
        normalized.erase(normalized.begin());
    }
    return normalized;
}

std::string ExtractTableName(const std::string& sqlFragment) {
    auto start = sqlFragment.find_first_not_of(' ');
    if (start == std::string::npos) {
        return {};
    }

    if (sqlFragment[start] == '`') {
        auto end = sqlFragment.find('`', start + 1);
        if (end == std::string::npos) {
            return sqlFragment.substr(start + 1);
        }
        return sqlFragment.substr(start + 1, end - start - 1);
    }

    auto end = sqlFragment.find_first_of(" (\t\n");
    if (end == std::string::npos) {
        return sqlFragment.substr(start);
    }
    return sqlFragment.substr(start, end - start);
}

StatementType IdentifyStatement(const std::string& sql, MariaDBConnection* connection, std::string& tableName) {
    if (sql.find("room_address LIKE CONCAT") != std::string::npos) {
        return StatementType::SelectRooms;
    }

    if (sql.find("INSERT INTO room ") != std::string::npos) {
        tableName = "room";
        return StatementType::InsertRoom;
    }

    if (sql.find("INSERT INTO persistent_message") != std::string::npos) {
        tableName = "persistent_message";
        return StatementType::InsertPersistentMessage;
    }

    if (sql.find("FROM persistent_message WHERE avatar_id = @avatar_id") != std::string::npos
        && sql.find("status IN (1, 2, 3)") != std::string::npos) {
        tableName = "persistent_message";
        return StatementType::SelectPersistentHeaders;
    }

    if (sql.find("FROM persistent_message WHERE id = @message_id") != std::string::npos) {
        tableName = "persistent_message";
        return StatementType::SelectPersistentMessage;
    }

    if (sql.find("UPDATE persistent_message SET status = @status WHERE id = @message_id") != std::string::npos) {
        tableName = "persistent_message";
        return StatementType::UpdatePersistentStatus;
    }

    if (sql.find("UPDATE persistent_message SET status = @status WHERE avatar_id = @avatar_id") != std::string::npos
        && sql.find("category = @category") != std::string::npos) {
        tableName = "persistent_message";
        return StatementType::BulkUpdatePersistentStatus;
    }

    if (sql.rfind("SHOW COLUMNS FROM ", 0) == 0) {
        tableName = ExtractTableName(sql.substr(std::strlen("SHOW COLUMNS FROM ")));
        return StatementType::ShowColumns;
    }

    if (sql.rfind("INSERT INTO ", 0) == 0 && connection) {
        auto fragment = sql.substr(std::strlen("INSERT INTO "));
        tableName = ExtractTableName(fragment);

        if (!connection->userLinkTableName.empty()
            && fragment.find(connection->userLinkTableName) != std::string::npos) {
            return StatementType::WebsiteUserLink;
        }

        if (!connection->onlineStatusTableName.empty()
            && fragment.find(connection->onlineStatusTableName) != std::string::npos) {
            return StatementType::WebsiteOnlineStatus;
        }

        if (!connection->mailTableName.empty()
            && fragment.find(connection->mailTableName) != std::string::npos) {
            return StatementType::WebsiteMail;
        }
    }

    return StatementType::Unknown;
}

BoundValue* GetBoundValue(MariaDBStatement* stmt, int index) {
    if (!stmt || index <= 0) {
        return nullptr;
    }

    auto nameIt = stmt->indexToName.find(index);
    if (nameIt == std::end(stmt->indexToName)) {
        return nullptr;
    }

    return &stmt->boundValues[nameIt->second];
}

const BoundValue* FindBoundValue(const MariaDBStatement* stmt, const std::string& name) {
    if (!stmt) {
        return nullptr;
    }

    auto valueIt = stmt->boundValues.find(name);
    if (valueIt == std::end(stmt->boundValues)) {
        return nullptr;
    }
    return &valueIt->second;
}

int GetBoundInt(const MariaDBStatement* stmt, const std::string& name, int defaultValue = 0) {
    auto value = FindBoundValue(stmt, name);
    if (!value) {
        return defaultValue;
    }

    if (value->kind == BoundValue::Kind::Int) {
        return value->intValue;
    }

    if (value->kind == BoundValue::Kind::Text && value->textValue && !value->isNull) {
        return std::stoi(*value->textValue);
    }

    return defaultValue;
}

std::optional<std::string> GetBoundText(const MariaDBStatement* stmt, const std::string& name) {
    auto value = FindBoundValue(stmt, name);
    if (!value || value->kind != BoundValue::Kind::Text || value->isNull) {
        return std::nullopt;
    }
    return value->textValue;
}

FakeTimestampValue GetTimestampValue(const MariaDBStatement* stmt, const std::string& name) {
    FakeTimestampValue result;
    auto value = FindBoundValue(stmt, name);
    if (!value) {
        return result;
    }

    if (value->kind == BoundValue::Kind::Int) {
        result.isNull = false;
        result.intValue = value->intValue;
    } else if (value->kind == BoundValue::Kind::Text) {
        result.isNull = value->isNull;
        if (!value->isNull && value->textValue) {
            result.textValue = *value->textValue;
        }
    }

    return result;
}

const std::vector<uint8_t>* GetBoundBlob(const MariaDBStatement* stmt, const std::string& name) {
    auto value = FindBoundValue(stmt, name);
    if (!value || value->kind != BoundValue::Kind::Blob) {
        return nullptr;
    }
    return &value->blobValue;
}

FakePersistentMessageRow* FindPersistentMessage(MariaDBConnection* connection, uint32_t avatarId, uint32_t messageId) {
    if (!connection) {
        return nullptr;
    }

    auto it = std::find_if(std::begin(connection->persistentMessages), std::end(connection->persistentMessages),
        [avatarId, messageId](const FakePersistentMessageRow& row) {
            return row.avatarId == avatarId && row.id == messageId;
        });

    if (it == std::end(connection->persistentMessages)) {
        return nullptr;
    }

    return &*it;
}

FakeUserLinkRow* FindUserLink(MariaDBConnection* connection, uint32_t avatarId) {
    auto it = std::find_if(std::begin(connection->websiteUserLinks), std::end(connection->websiteUserLinks),
        [avatarId](const FakeUserLinkRow& row) { return row.avatarId == avatarId; });
    if (it == std::end(connection->websiteUserLinks)) {
        return nullptr;
    }
    return &*it;
}

FakeStatusRow* FindStatusRow(MariaDBConnection* connection, uint32_t avatarId) {
    auto it = std::find_if(std::begin(connection->websiteStatusRows), std::end(connection->websiteStatusRows),
        [avatarId](const FakeStatusRow& row) { return row.avatarId == avatarId; });
    if (it == std::end(connection->websiteStatusRows)) {
        return nullptr;
    }
    return &*it;
}

FakeMailRow* FindMailRow(MariaDBConnection* connection, uint32_t messageId) {
    auto it = std::find_if(std::begin(connection->websiteMailRows), std::end(connection->websiteMailRows),
        [messageId](const FakeMailRow& row) { return row.messageId == messageId; });
    if (it == std::end(connection->websiteMailRows)) {
        return nullptr;
    }
    return &*it;
}

} // namespace

int mariadb_open(const char*, MariaDBConnection** db) {
    if (!db) {
        return MARIADB_ERROR;
    }
    *db = new MariaDBConnection();
    return MARIADB_OK;
}

int mariadb_close(MariaDBConnection* db) {
    delete db;
    return MARIADB_OK;
}

const char* mariadb_errmsg(MariaDBConnection* db) {
    static const char* defaultMessage = "Unknown MariaDB error";
    if (!db) {
        return defaultMessage;
    }
    return db->lastError.c_str();
}

int mariadb_prepare(MariaDBConnection* db, const char* sql, int, MariaDBStatement** stmt, const char**) {
    if (!db || !sql || !stmt) {
        return MARIADB_ERROR;
    }

    auto* statement = new MariaDBStatement();
    statement->connection = db;
    statement->sql = sql;
    statement->type = IdentifyStatement(statement->sql, db, statement->tableName);

    db->lastPreparedSql = sql;
    db->preparedSql.push_back(sql);
    db->lastError = "OK";

    *stmt = statement;
    return MARIADB_OK;
}

int mariadb_bind_parameter_index(MariaDBStatement* stmt, const char* parameterName) {
    if (!stmt || !parameterName) {
        return 0;
    }

    auto name = NormalizeParameter(parameterName);
    auto existing = stmt->parameterIndexes.find(name);
    if (existing != std::end(stmt->parameterIndexes)) {
        return existing->second;
    }

    int index = static_cast<int>(stmt->parameterIndexes.size()) + 1;
    stmt->parameterIndexes.emplace(name, index);
    stmt->indexToName.emplace(index, name);
    return index;
}

int mariadb_bind_int(MariaDBStatement* stmt, int index, int value) {
    auto bound = GetBoundValue(stmt, index);
    if (!bound) {
        return MARIADB_OK;
    }

    bound->kind = BoundValue::Kind::Int;
    bound->intValue = value;
    bound->isNull = false;
    return MARIADB_OK;
}

int mariadb_bind_text(MariaDBStatement* stmt, int index, const char* value, int length, void (*)(void*)) {
    auto bound = GetBoundValue(stmt, index);
    if (!bound) {
        return MARIADB_OK;
    }

    bound->kind = BoundValue::Kind::Text;
    if (!value) {
        bound->isNull = true;
        bound->textValue.reset();
        return MARIADB_OK;
    }

    bound->isNull = false;
    if (length < 0) {
        length = static_cast<int>(std::strlen(value));
    }
    bound->textValue = std::string(value, static_cast<std::size_t>(length));
    return MARIADB_OK;
}

int mariadb_bind_blob(MariaDBStatement* stmt, int index, const void* value, int length, void (*)(void*)) {
    auto bound = GetBoundValue(stmt, index);
    if (!bound) {
        return MARIADB_OK;
    }

    bound->kind = BoundValue::Kind::Blob;
    bound->blobValue.clear();
    if (value && length > 0) {
        const auto* bytes = static_cast<const uint8_t*>(value);
        bound->blobValue.assign(bytes, bytes + length);
    }
    return MARIADB_OK;
}

int mariadb_step(MariaDBStatement* stmt) {
    if (!stmt || !stmt->connection) {
        return MARIADB_ERROR;
    }

    auto& connection = *stmt->connection;
    connection.lastError = "OK";

    switch (stmt->type) {
    case StatementType::SelectRooms: {
        if (!stmt->prepared) {
            stmt->prepared = true;
            stmt->currentIndex = 0;
            stmt->matchedRooms.clear();
            stmt->currentRoom = nullptr;

            auto baseAddress = GetBoundText(stmt, "baseAddress");
            if (!baseAddress) {
                connection.lastError = "baseAddress not bound";
                return MARIADB_ERROR;
            }

            for (const auto& row : connection.roomRows) {
                if (row.roomAddress.rfind(*baseAddress, 0) == 0) {
                    stmt->matchedRooms.push_back(&row);
                }
            }
        }

        if (stmt->currentIndex >= stmt->matchedRooms.size()) {
            stmt->currentRoom = nullptr;
            return MARIADB_DONE;
        }

        stmt->currentRoom = stmt->matchedRooms[stmt->currentIndex++];
        return MARIADB_ROW;
    }
    case StatementType::InsertRoom: {
        FakeRoomRow row;
        row.id = static_cast<int>(connection.nextInsertId++);
        row.creatorId = GetBoundInt(stmt, "creator_id");
        row.creatorName = GetBoundText(stmt, "creator_name").value_or("");
        row.creatorAddress = GetBoundText(stmt, "creator_address").value_or("");
        row.roomName = GetBoundText(stmt, "room_name").value_or("");
        row.roomTopic = GetBoundText(stmt, "room_topic").value_or("");
        row.roomPassword = GetBoundText(stmt, "room_password").value_or("");
        row.roomPrefix = GetBoundText(stmt, "room_prefix").value_or("");
        row.roomAddress = GetBoundText(stmt, "room_address").value_or("");
        row.roomAttributes = GetBoundInt(stmt, "room_attributes");
        row.roomMaxSize = GetBoundInt(stmt, "room_max_size");
        row.roomMessageId = GetBoundInt(stmt, "room_message_id");
        row.createdAt = GetBoundInt(stmt, "created_at");
        row.nodeLevel = GetBoundInt(stmt, "node_level");

        connection.lastInsertId = row.id;
        connection.insertedRooms.push_back(row);
        return MARIADB_DONE;
    }
    case StatementType::InsertPersistentMessage: {
        FakePersistentMessageRow row;
        row.id = static_cast<uint32_t>(connection.nextInsertId++);
        row.avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
        row.fromName = GetBoundText(stmt, "from_name").value_or("");
        row.fromAddress = GetBoundText(stmt, "from_address").value_or("");
        row.subject = GetBoundText(stmt, "subject").value_or("");
        row.sentTime = static_cast<uint32_t>(GetBoundInt(stmt, "sent_time"));
        row.status = static_cast<uint32_t>(GetBoundInt(stmt, "status"));
        row.folder = GetBoundText(stmt, "folder").value_or("");
        row.category = GetBoundText(stmt, "category").value_or("");
        row.message = GetBoundText(stmt, "message").value_or("");
        if (auto* blob = GetBoundBlob(stmt, "oob")) {
            row.oob = *blob;
        }

        connection.lastInsertId = row.id;
        connection.persistentMessages.push_back(std::move(row));
        return MARIADB_DONE;
    }
    case StatementType::SelectPersistentHeaders:
    case StatementType::SelectPersistentMessage: {
        if (!stmt->prepared) {
            stmt->prepared = true;
            stmt->matchedMessages.clear();
            stmt->currentIndex = 0;
            stmt->currentMessage = nullptr;

            uint32_t avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
            if (stmt->type == StatementType::SelectPersistentHeaders) {
                for (const auto& row : connection.persistentMessages) {
                    if (row.avatarId == avatarId
                        && (row.status == static_cast<uint32_t>(PersistentState::NEW)
                            || row.status == static_cast<uint32_t>(PersistentState::UNREAD)
                            || row.status == static_cast<uint32_t>(PersistentState::READ))) {
                        stmt->matchedMessages.push_back(&row);
                    }
                }
            } else {
                uint32_t messageId = static_cast<uint32_t>(GetBoundInt(stmt, "message_id"));
                for (const auto& row : connection.persistentMessages) {
                    if (row.avatarId == avatarId && row.id == messageId) {
                        stmt->matchedMessages.push_back(&row);
                        break;
                    }
                }
            }
        }

        if (stmt->currentIndex >= stmt->matchedMessages.size()) {
            stmt->currentMessage = nullptr;
            return MARIADB_DONE;
        }

        stmt->currentMessage = stmt->matchedMessages[stmt->currentIndex++];
        return MARIADB_ROW;
    }
    case StatementType::UpdatePersistentStatus: {
        uint32_t avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
        uint32_t messageId = static_cast<uint32_t>(GetBoundInt(stmt, "message_id"));
        auto* row = FindPersistentMessage(&connection, avatarId, messageId);
        if (row) {
            row->status = static_cast<uint32_t>(GetBoundInt(stmt, "status"));
        }
        return MARIADB_DONE;
    }
    case StatementType::BulkUpdatePersistentStatus: {
        uint32_t avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
        auto category = GetBoundText(stmt, "category").value_or("");
        auto newStatus = static_cast<uint32_t>(GetBoundInt(stmt, "status"));

        for (auto& row : connection.persistentMessages) {
            if (row.avatarId == avatarId && row.category == category) {
                row.status = newStatus;
            }
        }
        return MARIADB_DONE;
    }
    case StatementType::WebsiteUserLink: {
        uint32_t avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
        uint32_t userId = static_cast<uint32_t>(GetBoundInt(stmt, "user_id"));
        auto avatarName = GetBoundText(stmt, "avatar_name").value_or("");
        auto createdAt = GetTimestampValue(stmt, "created_at");
        auto updatedAt = GetTimestampValue(stmt, "updated_at");

        auto* existing = FindUserLink(&connection, avatarId);
        if (!existing) {
            FakeUserLinkRow row;
            row.avatarId = avatarId;
            row.userId = userId;
            row.avatarName = avatarName;
            row.createdAt = createdAt;
            row.updatedAt = updatedAt;
            row.createdAt.isNull = createdAt.isNull;
            row.updatedAt.isNull = updatedAt.isNull;
            connection.websiteUserLinks.push_back(std::move(row));
        } else {
            existing->userId = userId;
            existing->avatarName = avatarName;
            if (!createdAt.isNull && (existing->createdAt.isNull)) {
                existing->createdAt = createdAt;
            }
            if (!updatedAt.isNull) {
                existing->updatedAt = updatedAt;
            }
        }
        return MARIADB_DONE;
    }
    case StatementType::WebsiteOnlineStatus: {
        uint32_t avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
        uint32_t userId = static_cast<uint32_t>(GetBoundInt(stmt, "user_id"));
        auto avatarName = GetBoundText(stmt, "avatar_name").value_or("");
        uint32_t isOnline = static_cast<uint32_t>(GetBoundInt(stmt, "is_online"));
        auto lastLogin = GetTimestampValue(stmt, "last_login");
        auto lastLogout = GetTimestampValue(stmt, "last_logout");
        auto updatedAt = GetTimestampValue(stmt, "updated_at");
        auto createdAt = GetTimestampValue(stmt, "created_at");

        auto* existing = FindStatusRow(&connection, avatarId);
        if (!existing) {
            FakeStatusRow row;
            row.avatarId = avatarId;
            row.userId = userId;
            row.avatarName = avatarName;
            row.isOnline = isOnline;
            row.lastLogin = lastLogin.intValue.value_or(0);
            row.lastLogout = lastLogout.intValue.value_or(0);
            row.createdAt = createdAt;
            row.updatedAt = updatedAt;
            connection.websiteStatusRows.push_back(std::move(row));
        } else {
            existing->userId = userId;
            existing->avatarName = avatarName;
            existing->isOnline = isOnline;

            if (lastLogin.intValue && *lastLogin.intValue != 0) {
                existing->lastLogin = *lastLogin.intValue;
            }
            if (lastLogout.intValue && *lastLogout.intValue != 0) {
                existing->lastLogout = *lastLogout.intValue;
            }
            if (!updatedAt.isNull) {
                existing->updatedAt = updatedAt;
            }
            if (existing->createdAt.isNull && !createdAt.isNull) {
                existing->createdAt = createdAt;
            }
        }
        return MARIADB_DONE;
    }
    case StatementType::WebsiteMail: {
        FakeMailRow row;
        row.avatarId = static_cast<uint32_t>(GetBoundInt(stmt, "avatar_id"));
        row.userId = static_cast<uint32_t>(GetBoundInt(stmt, "user_id"));
        row.avatarName = GetBoundText(stmt, "avatar_name").value_or("");
        row.messageId = static_cast<uint32_t>(GetBoundInt(stmt, "message_id"));
        row.senderName = GetBoundText(stmt, "sender_name").value_or("");
        row.senderAddress = GetBoundText(stmt, "sender_address").value_or("");
        row.subject = GetBoundText(stmt, "subject").value_or("");
        row.body = GetBoundText(stmt, "body").value_or("");
        row.oobText = GetBoundText(stmt, "oob").value_or("");
        row.sentTime = static_cast<uint32_t>(GetBoundInt(stmt, "sent_time"));
        row.createdAt = GetTimestampValue(stmt, "created_at");
        row.updatedAt = GetTimestampValue(stmt, "updated_at");
        row.status = static_cast<uint32_t>(GetBoundInt(stmt, "status"));

        auto* existing = FindMailRow(&connection, row.messageId);
        if (!existing) {
            connection.websiteMailRows.push_back(std::move(row));
        } else {
            *existing = std::move(row);
        }
        return MARIADB_DONE;
    }
    case StatementType::ShowColumns: {
        if (!stmt->prepared) {
            stmt->prepared = true;
            stmt->currentIndex = 0;
            stmt->returnedColumn = false;
            stmt->columnDefinition = nullptr;
            stmt->columnName = GetBoundText(stmt, "column_name").value_or("");

            auto tableIt = connection.columnDefinitions.find(stmt->tableName);
            if (tableIt != std::end(connection.columnDefinitions)) {
                auto columnIt = tableIt->second.find(stmt->columnName);
                if (columnIt != std::end(tableIt->second) && columnIt->second.exists) {
                    stmt->columnDefinition = &columnIt->second;
                }
            }
        }

        if (stmt->columnDefinition && !stmt->returnedColumn) {
            stmt->returnedColumn = true;
            return MARIADB_ROW;
        }

        return MARIADB_DONE;
    }
    case StatementType::Unknown:
    default:
        return MARIADB_ERROR;
    }
}

int mariadb_finalize(MariaDBStatement* stmt) {
    delete stmt;
    return MARIADB_OK;
}

int mariadb_column_int(MariaDBStatement* stmt, int column) {
    if (!stmt) {
        return 0;
    }

    switch (stmt->type) {
    case StatementType::SelectRooms:
        if (!stmt->currentRoom) {
            return 0;
        }
        switch (column) {
        case 0:
            return stmt->currentRoom->id;
        case 1:
            return stmt->currentRoom->creatorId;
        case 9:
            return stmt->currentRoom->roomAttributes;
        case 10:
            return stmt->currentRoom->roomMaxSize;
        case 11:
            return stmt->currentRoom->roomMessageId;
        case 12:
            return stmt->currentRoom->createdAt;
        case 13:
            return stmt->currentRoom->nodeLevel;
        default:
            return 0;
        }
    case StatementType::SelectPersistentHeaders:
    case StatementType::SelectPersistentMessage:
        if (!stmt->currentMessage) {
            return 0;
        }
        switch (column) {
        case 0:
            return static_cast<int>(stmt->currentMessage->id);
        case 1:
            return static_cast<int>(stmt->currentMessage->avatarId);
        case 5:
            return static_cast<int>(stmt->currentMessage->sentTime);
        case 6:
            return static_cast<int>(stmt->currentMessage->status);
        default:
            return 0;
        }
    default:
        return 0;
    }
}

const unsigned char* mariadb_column_text(MariaDBStatement* stmt, int column) {
    if (!stmt) {
        return nullptr;
    }

    switch (stmt->type) {
    case StatementType::SelectRooms:
        if (!stmt->currentRoom) {
            return nullptr;
        }
        switch (column) {
        case 2:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->creatorName.c_str());
        case 3:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->creatorAddress.c_str());
        case 4:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->roomName.c_str());
        case 5:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->roomTopic.c_str());
        case 6:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->roomPassword.c_str());
        case 7:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->roomPrefix.c_str());
        case 8:
            return reinterpret_cast<const unsigned char*>(stmt->currentRoom->roomAddress.c_str());
        default:
            return nullptr;
        }
    case StatementType::SelectPersistentHeaders:
    case StatementType::SelectPersistentMessage:
        if (!stmt->currentMessage) {
            return nullptr;
        }
        switch (column) {
        case 2:
            return reinterpret_cast<const unsigned char*>(stmt->currentMessage->fromName.c_str());
        case 3:
            return reinterpret_cast<const unsigned char*>(stmt->currentMessage->fromAddress.c_str());
        case 4:
            return reinterpret_cast<const unsigned char*>(stmt->currentMessage->subject.c_str());
        case 7:
            return reinterpret_cast<const unsigned char*>(stmt->currentMessage->folder.c_str());
        case 8:
            return reinterpret_cast<const unsigned char*>(stmt->currentMessage->category.c_str());
        case 9:
            return reinterpret_cast<const unsigned char*>(stmt->currentMessage->message.c_str());
        default:
            return nullptr;
        }
    case StatementType::ShowColumns:
        if (!stmt->columnDefinition || column != 1) {
            return nullptr;
        }
        return reinterpret_cast<const unsigned char*>(stmt->columnDefinition->type.c_str());
    default:
        return nullptr;
    }
}

const void* mariadb_column_blob(MariaDBStatement* stmt, int column) {
    if (!stmt || stmt->type != StatementType::SelectPersistentMessage || !stmt->currentMessage) {
        return nullptr;
    }

    if (column == 10) {
        return stmt->currentMessage->oob.data();
    }

    return nullptr;
}

int mariadb_column_bytes(MariaDBStatement* stmt, int column) {
    if (!stmt) {
        return 0;
    }

    switch (stmt->type) {
    case StatementType::SelectRooms:
        if (!stmt->currentRoom) {
            return 0;
        }
        switch (column) {
        case 2:
            return static_cast<int>(stmt->currentRoom->creatorName.size());
        case 3:
            return static_cast<int>(stmt->currentRoom->creatorAddress.size());
        case 4:
            return static_cast<int>(stmt->currentRoom->roomName.size());
        case 5:
            return static_cast<int>(stmt->currentRoom->roomTopic.size());
        case 6:
            return static_cast<int>(stmt->currentRoom->roomPassword.size());
        case 7:
            return static_cast<int>(stmt->currentRoom->roomPrefix.size());
        case 8:
            return static_cast<int>(stmt->currentRoom->roomAddress.size());
        default:
            return 0;
        }
    case StatementType::SelectPersistentHeaders:
    case StatementType::SelectPersistentMessage:
        if (!stmt->currentMessage) {
            return 0;
        }
        switch (column) {
        case 2:
            return static_cast<int>(stmt->currentMessage->fromName.size());
        case 3:
            return static_cast<int>(stmt->currentMessage->fromAddress.size());
        case 4:
            return static_cast<int>(stmt->currentMessage->subject.size());
        case 7:
            return static_cast<int>(stmt->currentMessage->folder.size());
        case 8:
            return static_cast<int>(stmt->currentMessage->category.size());
        case 9:
            return static_cast<int>(stmt->currentMessage->message.size());
        case 10:
            return static_cast<int>(stmt->currentMessage->oob.size());
        default:
            return 0;
        }
    default:
        return 0;
    }
}

std::int64_t mariadb_last_insert_rowid(MariaDBConnection* db) {
    if (!db) {
        return 0;
    }
    return db->lastInsertId;
}

