#pragma once

#include <cstdint>
#include <string>

// Forward declarations for the lightweight MariaDB wrapper implemented in
// MariaDB.cpp.  The previous SQLite compatibility layer has been replaced with
// dedicated MariaDB types and helpers.
struct MariaDBConnection;
struct MariaDBStatement;

struct MariaDBException {
    int code;
    std::string message;

    MariaDBException(const int result, char const* text)
        : code{result}
        , message{text} {}
};

constexpr int MARIADB_OK = 0;
constexpr int MARIADB_ERROR = 1;
constexpr int MARIADB_ROW = 100;
constexpr int MARIADB_DONE = 101;

#ifndef MARIADB_STATIC
#define MARIADB_STATIC reinterpret_cast<void (*)(void*)>(0)
#endif

#ifndef MARIADB_TRANSIENT
#define MARIADB_TRANSIENT reinterpret_cast<void (*)(void*)>(-1)
#endif

int mariadb_open(const char* connectionString, MariaDBConnection** db);
int mariadb_close(MariaDBConnection* db);
const char* mariadb_errmsg(MariaDBConnection* db);

int mariadb_prepare(MariaDBConnection* db, const char* sql, int, MariaDBStatement** stmt, const char** tail);
int mariadb_bind_parameter_index(MariaDBStatement* stmt, const char* parameterName);

int mariadb_bind_int(MariaDBStatement* stmt, int index, int value);
int mariadb_bind_text(MariaDBStatement* stmt, int index, const char* value, int length, void (*)(void*));
int mariadb_bind_blob(MariaDBStatement* stmt, int index, const void* value, int length, void (*)(void*));

int mariadb_step(MariaDBStatement* stmt);
int mariadb_reset(MariaDBStatement* stmt);
int mariadb_finalize(MariaDBStatement* stmt);

int mariadb_column_int(MariaDBStatement* stmt, int column);
const unsigned char* mariadb_column_text(MariaDBStatement* stmt, int column);
const void* mariadb_column_blob(MariaDBStatement* stmt, int column);
int mariadb_column_bytes(MariaDBStatement* stmt, int column);

std::int64_t mariadb_last_insert_rowid(MariaDBConnection* db);
