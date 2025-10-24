#include "MariaDB.hpp"

#include <cstdint>
#include <cstdlib>

struct MariaDBConnection {};
struct MariaDBStatement {};

int mariadb_open(const char*, MariaDBConnection** db) {
    if (!db) {
        return MARIADB_ERROR;
    }
    *db = new MariaDBConnection{};
    return MARIADB_OK;
}

int mariadb_close(MariaDBConnection* db) {
    delete db;
    return MARIADB_OK;
}

const char* mariadb_errmsg(MariaDBConnection*) {
    return "MariaDB support not available";
}

int mariadb_prepare(MariaDBConnection*, const char*, int, MariaDBStatement** stmt, const char**) {
    if (!stmt) {
        return MARIADB_ERROR;
    }
    *stmt = new MariaDBStatement{};
    return MARIADB_OK;
}

int mariadb_bind_parameter_index(MariaDBStatement*, const char*) { return 0; }

int mariadb_bind_int(MariaDBStatement*, int, int) { return MARIADB_OK; }

int mariadb_bind_text(MariaDBStatement*, int, const char*, int, void (*)(void*)) { return MARIADB_OK; }

int mariadb_bind_blob(MariaDBStatement*, int, const void*, int, void (*)(void*)) { return MARIADB_OK; }

int mariadb_step(MariaDBStatement*) { return MARIADB_DONE; }

int mariadb_reset(MariaDBStatement*) { return MARIADB_OK; }

int mariadb_finalize(MariaDBStatement* stmt) {
    delete stmt;
    return MARIADB_OK;
}

int mariadb_column_int(MariaDBStatement*, int) { return 0; }

const unsigned char* mariadb_column_text(MariaDBStatement*, int) { return nullptr; }

const void* mariadb_column_blob(MariaDBStatement*, int) { return nullptr; }

int mariadb_column_bytes(MariaDBStatement*, int) { return 0; }

std::int64_t mariadb_last_insert_rowid(MariaDBConnection*) { return 0; }

