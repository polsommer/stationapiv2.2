#include "MariaDB.hpp"

#include <mysql.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct MariaDBBindingValue {
    enum class Type {
        None,
        Int,
        Text,
        Blob,
    };

    Type type{Type::None};
    std::int64_t intValue{0};
    std::string textValue;
    std::vector<unsigned char> blobValue;
    bool isNull{false};
};

struct MariaDBConnection {
    MYSQL* handle{nullptr};
    std::string lastError{"OK"};
    std::string host{"127.0.0.1"};
    unsigned int port{3306};
    std::string user;
    std::string password;
    std::string database;
    std::string socketPath;
};

struct MariaDBStatement {
    MariaDBConnection* connection{nullptr};
    std::string sql;
    std::vector<std::string> segments;
    std::vector<std::size_t> placeholderToLogicalIndex;
    std::unordered_map<std::string, std::size_t> logicalIndexByName;
    std::vector<MariaDBBindingValue> bindings;

    MYSQL_RES* result{nullptr};
    MYSQL_ROW currentRow{nullptr};
    std::vector<unsigned long> currentLengths;
    bool executed{false};
    bool isSelect{false};
    std::string lastQuery;
    MYSQL* connectionHandleAtExecution{nullptr};
};

namespace {

std::once_flag mysqlInitFlag;

void EnsureMariaDBInitialized() {
    std::call_once(mysqlInitFlag, []() {
        mysql_library_init(0, nullptr, nullptr);
        std::atexit([]() { mysql_library_end(); });
    });
}

struct ConnectionInfo {
    std::string host{"127.0.0.1"};
    unsigned int port{3306};
    std::string user;
    std::string password;
    std::string database;
    std::string socketPath;
};

std::string ToLower(const std::string& input) {
    std::string out{input};
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string Trim(const std::string& input) {
    auto begin = input.find_first_not_of(" \t\n\r");
    auto end = input.find_last_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }
    return input.substr(begin, end - begin + 1);
}

ConnectionInfo ParseConnectionString(const std::string& connectionString) {
    ConnectionInfo info;

    auto trimmed = Trim(connectionString);
    if (trimmed.rfind("mariadb://", 0) == 0 || trimmed.rfind("mysql://", 0) == 0) {
        // URI style: mariadb://user:pass@host:port/schema
        std::string uriBody = trimmed.substr(trimmed.find("//") + 2);
        std::string credentialsPart;
        std::string hostPart;

        auto atPos = uriBody.find('@');
        if (atPos != std::string::npos) {
            credentialsPart = uriBody.substr(0, atPos);
            hostPart = uriBody.substr(atPos + 1);
        } else {
            hostPart = uriBody;
        }

        if (!credentialsPart.empty()) {
            auto colonPos = credentialsPart.find(':');
            if (colonPos == std::string::npos) {
                info.user = credentialsPart;
            } else {
                info.user = credentialsPart.substr(0, colonPos);
                info.password = credentialsPart.substr(colonPos + 1);
            }
        }

        auto slashPos = hostPart.find('/');
        if (slashPos != std::string::npos) {
            info.database = hostPart.substr(slashPos + 1);
            hostPart = hostPart.substr(0, slashPos);
        }

        auto colonPos = hostPart.find(':');
        if (colonPos == std::string::npos) {
            if (!hostPart.empty()) {
                info.host = hostPart;
            }
        } else {
            info.host = hostPart.substr(0, colonPos);
            auto portStr = hostPart.substr(colonPos + 1);
            if (!portStr.empty()) {
                info.port = static_cast<unsigned int>(std::stoul(portStr));
            }
        }

        return info;
    }

    std::stringstream ss(trimmed);
    std::string token;
    while (std::getline(ss, token, ';')) {
        auto equalPos = token.find('=');
        if (equalPos == std::string::npos) {
            continue;
        }

        auto key = ToLower(Trim(token.substr(0, equalPos)));
        auto value = Trim(token.substr(equalPos + 1));

        if (key == "host") {
            if (!value.empty()) {
                info.host = value;
            }
        } else if (key == "port") {
            if (!value.empty()) {
                info.port = static_cast<unsigned int>(std::stoul(value));
            }
        } else if (key == "user" || key == "username") {
            info.user = value;
        } else if (key == "password" || key == "passwd" || key == "pwd") {
            info.password = value;
        } else if (key == "database" || key == "schema") {
            info.database = value;
        } else if (key == "socket" || key == "socket_path") {
            info.socketPath = value;
        }
    }

    return info;
}

void SetError(MariaDBConnection* db, const std::string& message) {
    if (db) {
        db->lastError = message;
    }
}

bool Connect(MariaDBConnection* connection) {
    if (!connection) {
        return false;
    }

    if (connection->handle) {
        mysql_close(connection->handle);
        connection->handle = nullptr;
    }

    connection->handle = mysql_init(nullptr);
    if (!connection->handle) {
        SetError(connection, "Failed to initialize MariaDB handle");
        return false;
    }

    bool reconnect = true;
    mysql_options(connection->handle, MYSQL_OPT_RECONNECT, &reconnect);

    MYSQL* result = mysql_real_connect(connection->handle, connection->host.c_str(), connection->user.c_str(), connection->password.c_str(),
        connection->database.c_str(), connection->port, connection->socketPath.empty() ? nullptr : connection->socketPath.c_str(), 0);

    if (!result) {
        SetError(connection, mysql_error(connection->handle));
        mysql_close(connection->handle);
        connection->handle = nullptr;
        return false;
    }

    mysql_set_character_set(connection->handle, "utf8mb4");
    SetError(connection, "OK");
    return true;
}

bool EnsureConnection(MariaDBConnection* connection) {
    if (!connection) {
        return false;
    }

    if (connection->handle && mysql_ping(connection->handle) == 0) {
        return true;
    }

    if (connection->handle) {
        SetError(connection, mysql_error(connection->handle));
    }

    return Connect(connection);
}

std::string EscapeText(MariaDBConnection* db, const std::string& value) {
    if (!db || !db->handle) {
        return value;
    }

    if (value.empty()) {
        return "''";
    }

    std::string buffer;
    buffer.resize(value.size() * 2 + 1);
    auto length = mysql_real_escape_string(db->handle, &buffer[0], value.c_str(), static_cast<unsigned long>(value.size()));
    buffer.resize(length);

    return "'" + buffer + "'";
}

std::string FormatBlob(const std::vector<unsigned char>& blob) {
    if (blob.empty()) {
        return "X''";
    }

    static const char hexDigits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(blob.size() * 2 + 2);
    result.append("0x");
    for (auto byte : blob) {
        result.push_back(hexDigits[(byte >> 4) & 0x0F]);
        result.push_back(hexDigits[byte & 0x0F]);
    }
    return result;
}

std::string RenderQuery(MariaDBStatement* stmt) {
    std::string query;
    query.reserve(stmt->sql.size() + 32);

    auto placeholderCount = stmt->placeholderToLogicalIndex.size();
    for (std::size_t i = 0; i < placeholderCount; ++i) {
        query += stmt->segments[i];
        auto logicalIndex = stmt->placeholderToLogicalIndex[i];
        const auto& binding = stmt->bindings[logicalIndex - 1];

        if (binding.isNull || binding.type == MariaDBBindingValue::Type::None) {
            query += "NULL";
            continue;
        }

        switch (binding.type) {
        case MariaDBBindingValue::Type::Int:
            query += std::to_string(binding.intValue);
            break;
        case MariaDBBindingValue::Type::Text:
            query += EscapeText(stmt->connection, binding.textValue);
            break;
        case MariaDBBindingValue::Type::Blob:
            query += FormatBlob(binding.blobValue);
            break;
        default:
            query += "NULL";
            break;
        }
    }

    if (!stmt->segments.empty()) {
        query += stmt->segments.back();
    }

    return query;
}

void ClearResult(MariaDBStatement* stmt) {
    if (stmt->result) {
        mysql_free_result(stmt->result);
        stmt->result = nullptr;
    }
    stmt->currentRow = nullptr;
    stmt->currentLengths.clear();
}

} // namespace

int mariadb_open(const char* connectionString, MariaDBConnection** db) {
    if (!db) {
        return MARIADB_ERROR;
    }

    *db = nullptr;

    EnsureMariaDBInitialized();

    auto* connection = new MariaDBConnection();
    *db = connection;

    ConnectionInfo info;
    try {
        info = ParseConnectionString(connectionString ? connectionString : "");
    } catch (const std::exception& ex) {
        SetError(connection, ex.what());
        return MARIADB_ERROR;
    }
    if (info.user.empty() || info.database.empty()) {
        SetError(connection, "MariaDB connection string must include user and database");
        return MARIADB_ERROR;
    }

    connection->host = info.host;
    connection->port = info.port;
    connection->user = info.user;
    connection->password = info.password;
    connection->database = info.database;
    connection->socketPath = info.socketPath;

    if (!Connect(connection)) {
        return MARIADB_ERROR;
    }

    return MARIADB_OK;
}

int mariadb_close(MariaDBConnection* db) {
    if (!db) {
        return MARIADB_OK;
    }

    if (db->handle) {
        mysql_close(db->handle);
        db->handle = nullptr;
    }

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

int mariadb_prepare(MariaDBConnection* db, const char* sql, int, MariaDBStatement** stmt, const char** tail) {
    if (tail) {
        *tail = nullptr;
    }

    if (!db || !sql || !stmt) {
        return MARIADB_ERROR;
    }

    if (!EnsureConnection(db)) {
        return MARIADB_ERROR;
    }

    auto* statement = new MariaDBStatement();
    statement->connection = db;
    statement->sql = sql;
    statement->segments.clear();
    statement->segments.emplace_back();

    bool inStringLiteral = false;
    const std::string sqlString{sql};

    for (std::size_t i = 0; i < sqlString.size();) {
        char c = sqlString[i];
        if (c == '\'' && !inStringLiteral) {
            inStringLiteral = true;
            statement->segments.back().push_back(c);
            ++i;
            continue;
        }
        if (c == '\'' && inStringLiteral) {
            inStringLiteral = false;
            statement->segments.back().push_back(c);
            ++i;
            continue;
        }

        if (!inStringLiteral && c == '@') {
            std::size_t j = i + 1;
            std::string name;
            while (j < sqlString.size()) {
                char next = sqlString[j];
                if (std::isalnum(static_cast<unsigned char>(next)) || next == '_') {
                    name.push_back(next);
                    ++j;
                } else {
                    break;
                }
            }

            if (!name.empty()) {
                auto existing = statement->logicalIndexByName.find(name);
                std::size_t logicalIndex = 0;
                if (existing == statement->logicalIndexByName.end()) {
                    logicalIndex = statement->logicalIndexByName.size() + 1;
                    statement->logicalIndexByName.emplace(name, logicalIndex);
                    statement->bindings.resize(statement->logicalIndexByName.size());
                } else {
                    logicalIndex = existing->second;
                }

                statement->placeholderToLogicalIndex.push_back(logicalIndex);
                statement->segments.emplace_back();
                i = j;
                continue;
            }
        }

        statement->segments.back().push_back(c);
        ++i;
    }

    if (tail) {
        *tail = sql + sqlString.size();
    }

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
    auto it = stmt->logicalIndexByName.find(key);
    if (it == stmt->logicalIndexByName.end()) {
        return 0;
    }
    return static_cast<int>(it->second);
}

int mariadb_bind_int(MariaDBStatement* stmt, int index, int value) {
    if (!stmt || index <= 0 || static_cast<std::size_t>(index) > stmt->bindings.size()) {
        return MARIADB_ERROR;
    }
    auto& binding = stmt->bindings[index - 1];
    binding.type = MariaDBBindingValue::Type::Int;
    binding.intValue = value;
    binding.isNull = false;
    return MARIADB_OK;
}

int mariadb_bind_text(MariaDBStatement* stmt, int index, const char* value, int length, void (*)(void*)) {
    if (!stmt || index <= 0 || static_cast<std::size_t>(index) > stmt->bindings.size()) {
        return MARIADB_ERROR;
    }
    auto& binding = stmt->bindings[index - 1];
    binding.type = MariaDBBindingValue::Type::Text;
    if (!value) {
        binding.textValue.clear();
        binding.isNull = true;
    } else {
        if (length < 0) {
            length = static_cast<int>(std::strlen(value));
        }
        binding.textValue.assign(value, value + length);
        binding.isNull = false;
    }
    return MARIADB_OK;
}

int mariadb_bind_blob(MariaDBStatement* stmt, int index, const void* value, int length, void (*)(void*)) {
    if (!stmt || index <= 0 || static_cast<std::size_t>(index) > stmt->bindings.size()) {
        return MARIADB_ERROR;
    }
    auto& binding = stmt->bindings[index - 1];
    binding.type = MariaDBBindingValue::Type::Blob;
    binding.blobValue.clear();
    if (!value) {
        binding.isNull = true;
    } else {
        if (length < 0) {
            length = 0;
        }
        const auto* bytes = static_cast<const unsigned char*>(value);
        binding.blobValue.assign(bytes, bytes + length);
        binding.isNull = false;
    }
    return MARIADB_OK;
}

int mariadb_step(MariaDBStatement* stmt) {
    if (!stmt || !stmt->connection) {
        return MARIADB_ERROR;
    }

    if (!EnsureConnection(stmt->connection)) {
        return MARIADB_ERROR;
    }

    if (stmt->executed && stmt->connectionHandleAtExecution != stmt->connection->handle) {
        ClearResult(stmt);
        stmt->executed = false;
        stmt->isSelect = false;
        stmt->connectionHandleAtExecution = nullptr;
    }

    if (!stmt->executed) {
        stmt->connectionHandleAtExecution = stmt->connection->handle;

        ClearResult(stmt);
        stmt->lastQuery = RenderQuery(stmt);

        if (mysql_query(stmt->connection->handle, stmt->lastQuery.c_str()) != 0) {
            SetError(stmt->connection, mysql_error(stmt->connection->handle));
            return MARIADB_ERROR;
        }

        SetError(stmt->connection, "OK");

        stmt->executed = true;
        auto fieldCount = mysql_field_count(stmt->connection->handle);
        stmt->isSelect = fieldCount > 0;
        if (stmt->isSelect) {
            stmt->result = mysql_store_result(stmt->connection->handle);
            if (!stmt->result) {
                SetError(stmt->connection, mysql_error(stmt->connection->handle));
                stmt->executed = false;
                stmt->currentRow = nullptr;
                stmt->currentLengths.clear();
                return MARIADB_ERROR;
            }
            stmt->currentRow = mysql_fetch_row(stmt->result);
            if (!stmt->currentRow) {
                stmt->currentLengths.clear();
                SetError(stmt->connection, "OK");
                return MARIADB_DONE;
            }
            auto lengths = mysql_fetch_lengths(stmt->result);
            stmt->currentLengths.assign(lengths, lengths + mysql_num_fields(stmt->result));
            SetError(stmt->connection, "OK");
            return MARIADB_ROW;
        }

        SetError(stmt->connection, "OK");
        stmt->currentRow = nullptr;
        stmt->currentLengths.clear();
        return MARIADB_DONE;
    }

    if (!stmt->isSelect || !stmt->result) {
        SetError(stmt->connection, "OK");
        return MARIADB_DONE;
    }

    stmt->currentRow = mysql_fetch_row(stmt->result);
    if (!stmt->currentRow) {
        stmt->currentLengths.clear();
        SetError(stmt->connection, "OK");
        return MARIADB_DONE;
    }
    auto lengths = mysql_fetch_lengths(stmt->result);
    stmt->currentLengths.assign(lengths, lengths + mysql_num_fields(stmt->result));
    SetError(stmt->connection, "OK");
    return MARIADB_ROW;
}

int mariadb_finalize(MariaDBStatement* stmt) {
    if (!stmt) {
        return MARIADB_OK;
    }

    ClearResult(stmt);
    stmt->executed = false;
    stmt->isSelect = false;
    stmt->connectionHandleAtExecution = nullptr;
    delete stmt;
    return MARIADB_OK;
}

int mariadb_column_int(MariaDBStatement* stmt, int column) {
    if (!stmt || !stmt->currentRow || column < 0) {
        return 0;
    }
    auto numFields = stmt->currentLengths.size();
    if (static_cast<std::size_t>(column) >= numFields) {
        return 0;
    }
    const char* value = stmt->currentRow[column];
    if (!value) {
        return 0;
    }
    return std::stoi(value);
}

const unsigned char* mariadb_column_text(MariaDBStatement* stmt, int column) {
    if (!stmt || !stmt->currentRow || column < 0) {
        return nullptr;
    }
    auto numFields = stmt->currentLengths.size();
    if (static_cast<std::size_t>(column) >= numFields) {
        return nullptr;
    }
    return reinterpret_cast<const unsigned char*>(stmt->currentRow[column]);
}

const void* mariadb_column_blob(MariaDBStatement* stmt, int column) {
    if (!stmt || !stmt->currentRow || column < 0) {
        return nullptr;
    }
    auto numFields = stmt->currentLengths.size();
    if (static_cast<std::size_t>(column) >= numFields) {
        return nullptr;
    }
    return stmt->currentRow[column];
}

int mariadb_column_bytes(MariaDBStatement* stmt, int column) {
    if (!stmt || column < 0 || static_cast<std::size_t>(column) >= stmt->currentLengths.size()) {
        return 0;
    }
    return static_cast<int>(stmt->currentLengths[column]);
}

std::int64_t mariadb_last_insert_rowid(MariaDBConnection* db) {
    if (!db || !db->handle) {
        return 0;
    }
    return static_cast<std::int64_t>(mysql_insert_id(db->handle));
}
