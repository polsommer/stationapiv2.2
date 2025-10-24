#include "FakeMariaDB.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

struct MariaDBStatement {
    MariaDBConnection* connection{nullptr};
    std::string sql;
    std::optional<std::string> baseAddress;
    std::vector<const FakeRoomRow*> matchedRows;
    std::size_t currentIndex{0};
    const FakeRoomRow* currentRow{nullptr};
    bool prepared{false};
};

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

    *stmt = statement;
    return MARIADB_OK;
}

int mariadb_bind_parameter_index(MariaDBStatement*, const char* parameterName) {
    if (!parameterName) {
        return 0;
    }

    std::string name{parameterName};
    if (!name.empty() && name.front() == '@') {
        name.erase(name.begin());
    }

    if (name == "baseAddress") {
        return 1;
    }

    return 0;
}

int mariadb_bind_int(MariaDBStatement*, int, int) {
    return MARIADB_OK;
}

int mariadb_bind_text(MariaDBStatement* stmt, int index, const char* value, int length, void (*)(void*)) {
    if (!stmt || index != 1) {
        return MARIADB_ERROR;
    }

    if (!value) {
        stmt->baseAddress.reset();
        return MARIADB_OK;
    }

    if (length < 0) {
        length = static_cast<int>(std::strlen(value));
    }

    stmt->baseAddress = std::string(value, static_cast<std::size_t>(length));
    return MARIADB_OK;
}

int mariadb_bind_blob(MariaDBStatement*, int, const void*, int, void (*)(void*)) {
    return MARIADB_OK;
}

int mariadb_step(MariaDBStatement* stmt) {
    if (!stmt || !stmt->connection) {
        return MARIADB_ERROR;
    }

    if (!stmt->prepared) {
        stmt->prepared = true;
        stmt->currentIndex = 0;
        stmt->matchedRows.clear();

        if (!stmt->baseAddress.has_value()) {
            stmt->connection->lastError = "baseAddress not bound";
            return MARIADB_ERROR;
        }

        const std::string& prefix = *stmt->baseAddress;
        for (const auto& row : stmt->connection->roomRows) {
            if (row.roomAddress.rfind(prefix, 0) == 0) {
                stmt->matchedRows.push_back(&row);
            }
        }
    }

    if (stmt->currentIndex >= stmt->matchedRows.size()) {
        stmt->currentRow = nullptr;
        stmt->connection->lastError = "OK";
        return MARIADB_DONE;
    }

    stmt->currentRow = stmt->matchedRows[stmt->currentIndex++];
    stmt->connection->lastError = "OK";
    return MARIADB_ROW;
}

int mariadb_finalize(MariaDBStatement* stmt) {
    delete stmt;
    return MARIADB_OK;
}

int mariadb_column_int(MariaDBStatement* stmt, int column) {
    if (!stmt || !stmt->currentRow) {
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
    if (!stmt || !stmt->currentRow) {
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
    if (!stmt || !stmt->currentRow) {
        return 0;
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
        return 0;
    }

    return static_cast<int>(value->size());
}

std::int64_t mariadb_last_insert_rowid(MariaDBConnection*) {
    return 0;
}

