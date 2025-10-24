#include "FakeMariaDB.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <utility>

namespace {

enum class StatementType {
    Unknown,
    RoomSelect,
    ShowColumns,
    UserLinkUpsert,
    StatusUpsert,
    MailUpsert,
};

struct BoundParameter {
    enum class Kind {
        None,
        Int,
        Text,
        Blob,
    };

    Kind kind{Kind::None};
    bool isNull{true};
    int intValue{0};
    std::string textValue;

    void Reset() {
        kind = Kind::None;
        isNull = true;
        intValue = 0;
        textValue.clear();
    }
};

struct MariaDBStatement {
    MariaDBConnection* connection{nullptr};
    std::string sql;
    StatementType type{StatementType::Unknown};
    std::string tableName;
    std::vector<std::string> parameterNames;
    std::unordered_map<std::string, int> parameterIndex;
    std::vector<BoundParameter> bindings;

    // Result set state for room selection
    std::vector<const FakeRoomRow*> matchedRows;
    std::size_t currentIndex{0};
    const FakeRoomRow* currentRow{nullptr};

    // Result set state for SHOW COLUMNS
    bool showColumnHasRow{false};
    bool showColumnReturned{false};
    std::string showColumnType;

    bool executed{false};
};

std::string Trim(const std::string& value) {
    auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = value.find_last_not_of(" \t\n\r");
    return value.substr(begin, end - begin + 1);
}

std::string StripBackticks(const std::string& identifier) {
    std::string out;
    out.reserve(identifier.size());
    for (char ch : identifier) {
        if (ch != '`') {
            out.push_back(ch);
        }
    }
    return out;
}

std::string ExtractTableName(const std::string& sql, const std::string& prefix, const std::string& terminator) {
    auto prefixPos = sql.find(prefix);
    if (prefixPos == std::string::npos) {
        return {};
    }

    prefixPos += prefix.size();
    auto begin = sql.find_first_not_of(" \t", prefixPos);
    if (begin == std::string::npos) {
        return {};
    }

    std::size_t end = std::string::npos;
    if (!terminator.empty()) {
        end = sql.find(terminator, begin);
    }
    if (end == std::string::npos) {
        end = sql.find_first_of(" \t(\n\r", begin);
        if (end == std::string::npos) {
            end = sql.size();
        }
    }

    auto raw = sql.substr(begin, end - begin);
    return StripBackticks(Trim(raw));
}

void ParseParameterNames(MariaDBStatement* stmt) {
    if (!stmt) {
        return;
    }

    const auto& sql = stmt->sql;
    std::size_t i = 0;
    while (i < sql.size()) {
        if (sql[i] != '@') {
            ++i;
            continue;
        }

        std::size_t j = i + 1;
        std::string name;
        while (j < sql.size()) {
            char ch = sql[j];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
                name.push_back(ch);
                ++j;
            } else {
                break;
            }
        }

        if (!name.empty()) {
            if (!stmt->parameterIndex.count(name)) {
                int index = static_cast<int>(stmt->parameterNames.size() + 1);
                stmt->parameterNames.push_back(name);
                stmt->parameterIndex.emplace(name, index);
            }
            i = j;
        } else {
            ++i;
        }
    }

    stmt->bindings.resize(stmt->parameterNames.size());
}

void DetermineStatementType(MariaDBStatement* stmt) {
    if (!stmt || !stmt->connection) {
        return;
    }

    const auto& sql = stmt->sql;
    if (sql.find("SHOW COLUMNS FROM") != std::string::npos) {
        stmt->type = StatementType::ShowColumns;
        stmt->tableName = ExtractTableName(sql, "SHOW COLUMNS FROM", "LIKE");
        return;
    }

    if (sql.find("SELECT") != std::string::npos && sql.find("room_address") != std::string::npos) {
        stmt->type = StatementType::RoomSelect;
        return;
    }

    if (sql.find("INSERT INTO") != std::string::npos) {
        stmt->tableName = ExtractTableName(sql, "INSERT INTO", "");
        const auto& connection = *stmt->connection;
        if (stmt->tableName == connection.userLinkTableName) {
            stmt->type = StatementType::UserLinkUpsert;
        } else if (stmt->tableName == connection.statusTableName) {
            stmt->type = StatementType::StatusUpsert;
        } else if (stmt->tableName == connection.mailTableName) {
            stmt->type = StatementType::MailUpsert;
        }
        return;
    }

    stmt->type = StatementType::Unknown;
}

const BoundParameter* GetBinding(const MariaDBStatement* stmt, const std::string& name) {
    if (!stmt) {
        return nullptr;
    }
    auto it = stmt->parameterIndex.find(name);
    if (it == stmt->parameterIndex.end()) {
        return nullptr;
    }
    int index = it->second;
    if (index <= 0 || static_cast<std::size_t>(index) > stmt->bindings.size()) {
        return nullptr;
    }
    return &stmt->bindings[static_cast<std::size_t>(index - 1)];
}

std::optional<int> GetIntParameter(const MariaDBStatement* stmt, const std::string& name) {
    const auto* binding = GetBinding(stmt, name);
    if (!binding || binding->isNull) {
        return std::nullopt;
    }
    if (binding->kind == BoundParameter::Kind::Int) {
        return binding->intValue;
    }
    if (binding->kind == BoundParameter::Kind::Text) {
        try {
            return std::stoi(binding->textValue);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> GetTextParameter(const MariaDBStatement* stmt, const std::string& name) {
    const auto* binding = GetBinding(stmt, name);
    if (!binding || binding->isNull) {
        return std::nullopt;
    }
    if (binding->kind == BoundParameter::Kind::Text) {
        return binding->textValue;
    }
    if (binding->kind == BoundParameter::Kind::Int) {
        return std::to_string(binding->intValue);
    }
    return std::nullopt;
}

const FakeColumnDefinition* FindColumnDefinition(const MariaDBConnection* connection,
    const std::string& tableName, const std::string& columnName) {
    if (!connection) {
        return nullptr;
    }
    auto tableIt = connection->tableColumns.find(tableName);
    if (tableIt == connection->tableColumns.end()) {
        return nullptr;
    }
    auto columnIt = tableIt->second.find(columnName);
    if (columnIt == tableIt->second.end()) {
        return nullptr;
    }
    return &columnIt->second;
}

FakeTimestampValue BuildTimestampValue(const MariaDBStatement* stmt, const std::string& parameterName,
    const std::string& columnName) {
    FakeTimestampValue value;
    value.isNull = true;

    if (!stmt) {
        return value;
    }

    auto def = FindColumnDefinition(stmt->connection, stmt->tableName, columnName);
    if (def) {
        value.isDateTime = def->isDateTime;
    }

    const auto* binding = GetBinding(stmt, parameterName);
    if (!binding || binding->kind == BoundParameter::Kind::None) {
        return value;
    }

    if (binding->isNull) {
        return value;
    }

    switch (binding->kind) {
    case BoundParameter::Kind::Int:
        value.isNull = false;
        value.intValue = static_cast<uint32_t>(binding->intValue);
        value.textValue = std::to_string(binding->intValue);
        break;
    case BoundParameter::Kind::Text:
        value.isNull = false;
        value.textValue = binding->textValue;
        break;
    default:
        break;
    }

    return value;
}

void AssignTimestamp(FakeTimestampValue& target, const FakeTimestampValue& source) {
    target = source;
}

void AssignTimestampIfProvided(FakeTimestampValue& target, const FakeTimestampValue& source) {
    if (source.isNull) {
        return;
    }
    if (!source.isDateTime && source.intValue == 0) {
        return;
    }
    AssignTimestamp(target, source);
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
    db->lastPreparedSql = sql;
    db->lastError = "OK";

    ParseParameterNames(statement);
    DetermineStatementType(statement);

    *stmt = statement;
    return MARIADB_OK;
}

int mariadb_bind_parameter_index(MariaDBStatement* stmt, const char* parameterName) {
    if (!stmt || !parameterName) {
        return 0;
    }

    std::string key{parameterName};
    if (!key.empty() && key.front() == '@') {
        key.erase(key.begin());
    }

    auto it = stmt->parameterIndex.find(key);
    if (it == stmt->parameterIndex.end()) {
        return 0;
    }
    return it->second;
}

int mariadb_bind_int(MariaDBStatement* stmt, int index, int value) {
    if (!stmt || index <= 0) {
        return MARIADB_ERROR;
    }

    if (static_cast<std::size_t>(index) > stmt->bindings.size()) {
        stmt->bindings.resize(static_cast<std::size_t>(index));
    }

    auto& binding = stmt->bindings[static_cast<std::size_t>(index - 1)];
    binding.kind = BoundParameter::Kind::Int;
    binding.isNull = false;
    binding.intValue = value;
    binding.textValue.clear();
    return MARIADB_OK;
}

int mariadb_bind_text(MariaDBStatement* stmt, int index, const char* value, int length, void (*)(void*)) {
    if (!stmt || index <= 0) {
        return MARIADB_ERROR;
    }

    if (static_cast<std::size_t>(index) > stmt->bindings.size()) {
        stmt->bindings.resize(static_cast<std::size_t>(index));
    }

    auto& binding = stmt->bindings[static_cast<std::size_t>(index - 1)];
    binding.kind = BoundParameter::Kind::Text;

    if (!value) {
        binding.isNull = true;
        binding.textValue.clear();
        return MARIADB_OK;
    }

    binding.isNull = false;
    if (length < 0) {
        length = static_cast<int>(std::strlen(value));
    }
    binding.textValue.assign(value, static_cast<std::size_t>(length));
    return MARIADB_OK;
}

int mariadb_bind_blob(MariaDBStatement*, int, const void*, int, void (*)(void*)) {
    return MARIADB_OK;
}

int mariadb_step(MariaDBStatement* stmt) {
    if (!stmt || !stmt->connection) {
        return MARIADB_ERROR;
    }

    auto& connection = *stmt->connection;

    switch (stmt->type) {
    case StatementType::RoomSelect: {
        if (!stmt->executed) {
            stmt->executed = true;
            stmt->currentIndex = 0;
            stmt->matchedRows.clear();

            auto baseAddress = GetTextParameter(stmt, "baseAddress");
            if (!baseAddress) {
                connection.lastError = "baseAddress not bound";
                return MARIADB_ERROR;
            }

            for (const auto& row : connection.roomRows) {
                if (row.roomAddress.rfind(*baseAddress, 0) == 0) {
                    stmt->matchedRows.push_back(&row);
                }
            }
        }

        if (stmt->currentIndex >= stmt->matchedRows.size()) {
            stmt->currentRow = nullptr;
            connection.lastError = "OK";
            return MARIADB_DONE;
        }

        stmt->currentRow = stmt->matchedRows[stmt->currentIndex++];
        connection.lastError = "OK";
        return MARIADB_ROW;
    }

    case StatementType::ShowColumns: {
        if (!stmt->executed) {
            stmt->executed = true;
            stmt->showColumnReturned = false;
            stmt->showColumnHasRow = false;

            auto columnName = GetTextParameter(stmt, "column_name");
            if (columnName) {
                auto def = FindColumnDefinition(&connection, stmt->tableName, *columnName);
                if (def) {
                    stmt->showColumnHasRow = true;
                    stmt->showColumnType = def->typeName;
                }
            }
        }

        if (!stmt->showColumnHasRow || stmt->showColumnReturned) {
            connection.lastError = "OK";
            return MARIADB_DONE;
        }

        stmt->showColumnReturned = true;
        connection.lastError = "OK";
        return MARIADB_ROW;
    }

    case StatementType::UserLinkUpsert: {
        if (stmt->executed) {
            connection.lastError = "OK";
            return MARIADB_DONE;
        }

        stmt->executed = true;

        auto userId = GetIntParameter(stmt, "user_id").value_or(0);
        auto avatarId = GetIntParameter(stmt, "avatar_id").value_or(0);
        auto avatarName = GetTextParameter(stmt, "avatar_name").value_or("");
        auto createdAt = BuildTimestampValue(stmt, "created_at", "created_at");
        auto updatedAt = BuildTimestampValue(stmt, "updated_at", "updated_at");

        auto& rows = connection.userLinkRows[stmt->tableName];
        auto existing = std::find_if(rows.begin(), rows.end(), [avatarId](const FakeUserLinkRow& row) {
            return row.avatarId == avatarId;
        });

        if (existing == rows.end()) {
            FakeUserLinkRow row;
            row.userId = static_cast<uint32_t>(userId);
            row.avatarId = static_cast<uint32_t>(avatarId);
            row.avatarName = std::move(avatarName);
            row.createdAt = createdAt;
            row.updatedAt = updatedAt;
            rows.push_back(std::move(row));
        } else {
            existing->userId = static_cast<uint32_t>(userId);
            existing->avatarName = std::move(avatarName);
            if (!createdAt.isNull && existing->createdAt.isNull) {
                existing->createdAt = createdAt;
            }
            existing->updatedAt = updatedAt;
        }

        connection.lastError = "OK";
        return MARIADB_DONE;
    }

    case StatementType::StatusUpsert: {
        if (stmt->executed) {
            connection.lastError = "OK";
            return MARIADB_DONE;
        }

        stmt->executed = true;

        auto avatarId = GetIntParameter(stmt, "avatar_id").value_or(0);
        auto userId = GetIntParameter(stmt, "user_id").value_or(0);
        auto avatarName = GetTextParameter(stmt, "avatar_name").value_or("");
        auto isOnline = GetIntParameter(stmt, "is_online").value_or(0) != 0;
        auto lastLogin = BuildTimestampValue(stmt, "last_login", "last_login");
        auto lastLogout = BuildTimestampValue(stmt, "last_logout", "last_logout");
        auto updatedAt = BuildTimestampValue(stmt, "updated_at", "updated_at");
        auto createdAt = BuildTimestampValue(stmt, "created_at", "created_at");

        auto& rows = connection.statusRows[stmt->tableName];
        auto existing = std::find_if(rows.begin(), rows.end(), [avatarId](const FakeStatusRow& row) {
            return row.avatarId == static_cast<uint32_t>(avatarId);
        });

        if (existing == rows.end()) {
            FakeStatusRow row;
            row.avatarId = static_cast<uint32_t>(avatarId);
            row.userId = static_cast<uint32_t>(userId);
            row.avatarName = std::move(avatarName);
            row.isOnline = isOnline;
            row.lastLogin = lastLogin;
            row.lastLogout = lastLogout;
            row.updatedAt = updatedAt;
            row.createdAt = createdAt;
            rows.push_back(std::move(row));
        } else {
            existing->userId = static_cast<uint32_t>(userId);
            existing->avatarName = std::move(avatarName);
            existing->isOnline = isOnline;
            AssignTimestampIfProvided(existing->lastLogin, lastLogin);
            AssignTimestampIfProvided(existing->lastLogout, lastLogout);
            existing->updatedAt = updatedAt;
            if (!createdAt.isNull && existing->createdAt.isNull) {
                existing->createdAt = createdAt;
            }
        }

        connection.lastError = "OK";
        return MARIADB_DONE;
    }

    case StatementType::MailUpsert: {
        if (stmt->executed) {
            connection.lastError = "OK";
            return MARIADB_DONE;
        }

        stmt->executed = true;

        auto avatarId = GetIntParameter(stmt, "avatar_id").value_or(0);
        auto userId = GetIntParameter(stmt, "user_id").value_or(0);
        auto avatarName = GetTextParameter(stmt, "avatar_name").value_or("");
        auto messageId = GetIntParameter(stmt, "message_id").value_or(0);
        auto senderName = GetTextParameter(stmt, "sender_name").value_or("");
        auto senderAddress = GetTextParameter(stmt, "sender_address").value_or("");
        auto subject = GetTextParameter(stmt, "subject").value_or("");
        auto body = GetTextParameter(stmt, "body").value_or("");
        auto oob = GetTextParameter(stmt, "oob").value_or("");
        auto sentTime = GetIntParameter(stmt, "sent_time").value_or(0);
        auto createdAt = BuildTimestampValue(stmt, "created_at", "created_at");
        auto updatedAt = BuildTimestampValue(stmt, "updated_at", "updated_at");
        auto status = GetIntParameter(stmt, "status").value_or(0);

        auto& rows = connection.mailRows[stmt->tableName];
        auto existing = std::find_if(rows.begin(), rows.end(), [messageId](const FakeMailRow& row) {
            return row.messageId == static_cast<uint32_t>(messageId);
        });

        if (existing == rows.end()) {
            FakeMailRow row;
            row.avatarId = static_cast<uint32_t>(avatarId);
            row.userId = static_cast<uint32_t>(userId);
            row.avatarName = std::move(avatarName);
            row.messageId = static_cast<uint32_t>(messageId);
            row.senderName = std::move(senderName);
            row.senderAddress = std::move(senderAddress);
            row.subject = std::move(subject);
            row.body = std::move(body);
            row.oob = std::move(oob);
            row.sentTime = static_cast<uint32_t>(sentTime);
            row.createdAt = createdAt;
            row.updatedAt = updatedAt;
            row.status = static_cast<uint32_t>(status);
            rows.push_back(std::move(row));
        } else {
            existing->avatarId = static_cast<uint32_t>(avatarId);
            existing->userId = static_cast<uint32_t>(userId);
            existing->avatarName = std::move(avatarName);
            existing->senderName = std::move(senderName);
            existing->senderAddress = std::move(senderAddress);
            existing->subject = std::move(subject);
            existing->body = std::move(body);
            existing->oob = std::move(oob);
            existing->sentTime = static_cast<uint32_t>(sentTime);
            existing->createdAt = createdAt;
            existing->updatedAt = updatedAt;
            existing->status = static_cast<uint32_t>(status);
        }

        connection.lastError = "OK";
        return MARIADB_DONE;
    }

    case StatementType::Unknown:
    default:
        connection.lastError = "Unsupported statement";
        return MARIADB_ERROR;
    }
}

int mariadb_reset(MariaDBStatement* stmt) {
    if (!stmt) {
        return MARIADB_OK;
    }

    stmt->executed = false;
    stmt->currentIndex = 0;
    stmt->currentRow = nullptr;
    stmt->matchedRows.clear();
    stmt->showColumnHasRow = false;
    stmt->showColumnReturned = false;
    stmt->showColumnType.clear();
    for (auto& binding : stmt->bindings) {
        binding.Reset();
    }
    return MARIADB_OK;
}

int mariadb_finalize(MariaDBStatement* stmt) {
    delete stmt;
    return MARIADB_OK;
}

int mariadb_column_int(MariaDBStatement* stmt, int column) {
    if (!stmt || !stmt->currentRow) {
        return 0;
    }

    if (stmt->type != StatementType::RoomSelect) {
        return 0;
    }

    switch (column) {
    case 0:
        return stmt->currentRow->id;
    case 1:
        return stmt->currentRow->creatorId;
    case 9:
        return stmt->currentRow->roomAttributes;
    case 10:
        return stmt->currentRow->roomMaxSize;
    case 11:
        return stmt->currentRow->roomMessageId;
    case 12:
        return stmt->currentRow->createdAt;
    case 13:
        return stmt->currentRow->nodeLevel;
    default:
        return 0;
    }
}

const unsigned char* mariadb_column_text(MariaDBStatement* stmt, int column) {
    if (!stmt) {
        return nullptr;
    }

    if (stmt->type == StatementType::ShowColumns) {
        if (column == 1 && stmt->showColumnHasRow && stmt->showColumnReturned) {
            return reinterpret_cast<const unsigned char*>(stmt->showColumnType.c_str());
        }
        return nullptr;
    }

    if (!stmt->currentRow || stmt->type != StatementType::RoomSelect) {
        return nullptr;
    }

    const std::string* value = nullptr;
    switch (column) {
    case 2:
        value = &stmt->currentRow->creatorName;
        break;
    case 3:
        value = &stmt->currentRow->creatorAddress;
        break;
    case 4:
        value = &stmt->currentRow->roomName;
        break;
    case 5:
        value = &stmt->currentRow->roomTopic;
        break;
    case 6:
        value = &stmt->currentRow->roomPassword;
        break;
    case 7:
        value = &stmt->currentRow->roomPrefix;
        break;
    case 8:
        value = &stmt->currentRow->roomAddress;
        break;
    default:
        return nullptr;
    }

    return reinterpret_cast<const unsigned char*>(value->c_str());
}

const void* mariadb_column_blob(MariaDBStatement*, int) { return nullptr; }

int mariadb_column_bytes(MariaDBStatement* stmt, int column) {
    if (!stmt) {
        return 0;
    }

    if (stmt->type == StatementType::ShowColumns) {
        if (column == 1 && stmt->showColumnHasRow && stmt->showColumnReturned) {
            return static_cast<int>(stmt->showColumnType.size());
        }
        return 0;
    }

    if (!stmt->currentRow || stmt->type != StatementType::RoomSelect) {
        return 0;
    }

    const unsigned char* text = mariadb_column_text(stmt, column);
    if (!text) {
        return 0;
    }
    return static_cast<int>(std::strlen(reinterpret_cast<const char*>(text)));
}

std::int64_t mariadb_last_insert_rowid(MariaDBConnection*) { return 0; }

