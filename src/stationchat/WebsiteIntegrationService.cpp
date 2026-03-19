#include "WebsiteIntegrationService.hpp"

#include "ChatAvatar.hpp"
#include "MariaDB.hpp"
#include "PersistentMessage.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

std::string QuoteIdentifier(const std::string& identifier) {
    std::string quoted;
    std::size_t start = 0;
    bool firstPart = true;

    while (start <= identifier.size()) {
        auto dotPos = identifier.find('.', start);
        auto length = dotPos == std::string::npos ? identifier.size() - start : dotPos - start;
        std::string part = identifier.substr(start, length);

        if (!firstPart) {
            quoted += '.';
        }

        quoted += '`';
        for (char ch : part) {
            if (ch == '`') {
                quoted += "``";
            } else {
                quoted += ch;
            }
        }
        quoted += '`';

        firstPart = false;

        if (dotPos == std::string::npos) {
            break;
        }

        start = dotPos + 1;
    }

    return quoted;
}

std::string BuildUserLinkSql(const std::string& table, bool includeCreatedAt, bool includeUpdatedAt) {
    std::string columnList{"(user_id, avatar_id, avatar_name"};
    std::string values{"(@user_id, @avatar_id, @avatar_name"};

    if (includeCreatedAt) {
        columnList += ", created_at";
        values += ", @created_at";
    }

    if (includeUpdatedAt) {
        columnList += ", updated_at";
        values += ", @updated_at";
    }

    columnList += ')';
    values += ')';

    std::string sql = "INSERT INTO " + QuoteIdentifier(table) + " " + columnList + " VALUES " + values
        + " ON DUPLICATE KEY UPDATE user_id = VALUES(user_id), avatar_name = VALUES(avatar_name)";

    if (includeUpdatedAt) {
        sql += ", updated_at = VALUES(updated_at)";
    }

    if (includeCreatedAt) {
        sql += ", created_at = COALESCE(created_at, VALUES(created_at))";
    }

    return sql;
}

std::string BuildStatusSql(const std::string& table, bool includeCreatedAt, bool includeUpdatedAt) {
    std::string columnList{"(avatar_id, user_id, avatar_name, is_online, last_login, last_logout"};
    std::string values{"(@avatar_id, @user_id, @avatar_name, @is_online, @last_login, @last_logout"};

    if (includeUpdatedAt) {
        columnList += ", updated_at";
        values += ", @updated_at";
    }

    if (includeCreatedAt) {
        columnList += ", created_at";
        values += ", @created_at";
    }

    columnList += ')';
    values += ')';

    std::string sql = "INSERT INTO " + QuoteIdentifier(table) + " " + columnList + " VALUES " + values
        + " ON DUPLICATE KEY UPDATE user_id = VALUES(user_id), avatar_name = VALUES(avatar_name), "
          "is_online = VALUES(is_online), last_login = IF(VALUES(last_login) != 0, VALUES(last_login), last_login), "
          "last_logout = IF(VALUES(last_logout) != 0, VALUES(last_logout), last_logout)";

    if (includeUpdatedAt) {
        sql += ", updated_at = VALUES(updated_at)";
    }

    if (includeCreatedAt) {
        sql += ", created_at = COALESCE(created_at, VALUES(created_at))";
    }

    return sql;
}

std::string BuildMailSql(const std::string& table, bool includeCreatedAt, bool includeUpdatedAt) {
    std::string columnList{"(avatar_id, user_id, avatar_name, message_id, sender_name, sender_address, subject, body, oob, sent_time"};
    std::string values{"(@avatar_id, @user_id, @avatar_name, @message_id, @sender_name, @sender_address, @subject, @body, @oob, @sent_time"};

    if (includeCreatedAt) {
        columnList += ", created_at";
        values += ", @created_at";
    }

    if (includeUpdatedAt) {
        columnList += ", updated_at";
        values += ", @updated_at";
    }

    columnList += ", status)";
    values += ", @status)";

    std::string sql = "INSERT INTO " + QuoteIdentifier(table) + " " + columnList + " VALUES " + values
        + " ON DUPLICATE KEY UPDATE sender_name = VALUES(sender_name), sender_address = VALUES(sender_address), "
          "subject = VALUES(subject), body = VALUES(body), oob = VALUES(oob), sent_time = VALUES(sent_time), status = VALUES(status)";

    if (includeCreatedAt) {
        sql += ", created_at = VALUES(created_at)";
    }

    if (includeUpdatedAt) {
        sql += ", updated_at = VALUES(updated_at)";
    }

    return sql;
}

bool ContainsDateTimeType(const std::string& type) {
    std::string lowered;
    lowered.reserve(type.size());
    for (auto ch : type) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered.find("timestamp") != std::string::npos || lowered.find("datetime") != std::string::npos
        || lowered.find("date") != std::string::npos;
}

std::string BuildWebsiteConnectionString(const StationChatConfig& config) {
    const auto& website = config.websiteIntegration;

    if (!website.useSeparateDatabase) {
        return config.BuildDatabaseConnectionString();
    }

    auto host = website.databaseHost.empty() ? config.chatDatabaseHost : website.databaseHost;
    auto port = website.databasePort == 0 ? config.chatDatabasePort : website.databasePort;
    auto user = website.databaseUser.empty() ? config.chatDatabaseUser : website.databaseUser;
    auto password = website.databasePassword.empty() ? config.chatDatabasePassword : website.databasePassword;
    auto schema = website.databaseSchema.empty() ? config.chatDatabaseSchema : website.databaseSchema;
    auto socket = website.databaseSocket.empty() ? config.chatDatabaseSocket : website.databaseSocket;

    std::ostringstream stream;
    stream << "host=" << host;
    stream << ";port=" << port;
    stream << ";user=" << user;
    stream << ";password=" << password;
    stream << ";database=" << schema;
    if (!socket.empty()) {
        stream << ";socket=" << socket;
    }

    return stream.str();
}

} // namespace

WebsiteIntegrationService::WebsiteIntegrationService(MariaDBConnection* db, const StationChatConfig& config)
    : db_{db} {
    if (!db_) {
        return;
    }

    enabled_ = config.websiteIntegration.enabled;
    if (!enabled_) {
        return;
    }

    auto connectionString = BuildWebsiteConnectionString(config);
    if (config.websiteIntegration.useSeparateDatabase) {
        MariaDBConnection* integrationDb = nullptr;
        auto result = mariadb_open(connectionString.c_str(), &integrationDb);
        if (result != MARIADB_OK) {
            std::string errorMessage = integrationDb ? mariadb_errmsg(integrationDb) : "Unknown MariaDB error";
            if (integrationDb) {
                mariadb_close(integrationDb);
                integrationDb = nullptr;
            }
            throw std::runtime_error("Can't open website integration database connection: " + errorMessage);
        }

        db_ = integrationDb;
        ownsDatabase_ = true;
    }

    userLinkTable_ = config.websiteIntegration.userLinkTable;
    onlineStatusTable_ = config.websiteIntegration.onlineStatusTable;
    mailTable_ = config.websiteIntegration.mailTable;

    userLinkCreatedAt_ = InspectColumn(userLinkTable_, "created_at");
    userLinkUpdatedAt_ = InspectColumn(userLinkTable_, "updated_at");

    statusCreatedAt_ = InspectColumn(onlineStatusTable_, "created_at");
    statusUpdatedAt_ = InspectColumn(onlineStatusTable_, "updated_at");
    statusLoginAt_ = InspectColumn(onlineStatusTable_, "last_login");
    statusLogoutAt_ = InspectColumn(onlineStatusTable_, "last_logout");

    mailCreatedAt_ = InspectColumn(mailTable_, "created_at");
    mailUpdatedAt_ = InspectColumn(mailTable_, "updated_at");

    userLinkSql_ = BuildUserLinkSql(userLinkTable_, userLinkCreatedAt_.exists, userLinkUpdatedAt_.exists);
    statusSql_ = BuildStatusSql(onlineStatusTable_, statusCreatedAt_.exists, statusUpdatedAt_.exists);
    mailSql_ = BuildMailSql(mailTable_, mailCreatedAt_.exists, mailUpdatedAt_.exists);
}

WebsiteIntegrationService::~WebsiteIntegrationService() {
    if (ownsDatabase_) {
        mariadb_close(db_);
        db_ = nullptr;
    }
}

void WebsiteIntegrationService::RecordAvatarLogin(const ChatAvatar& avatar) {
    if (!enabled_) {
        return;
    }

    EnsureUserLink(avatar);
    UpdateOnlineStatus(avatar, true);
}

void WebsiteIntegrationService::RecordAvatarLogout(const ChatAvatar& avatar) {
    if (!enabled_) {
        return;
    }

    UpdateOnlineStatus(avatar, false);
}

void WebsiteIntegrationService::RecordPersistentMessage(
    const ChatAvatar& destAvatar, const PersistentMessage& message) {
    if (!enabled_) {
        return;
    }

    EnsureUserLink(destAvatar);

    MariaDBStatement* stmt;
    auto result = mariadb_prepare(db_, mailSql_.c_str(), -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    auto avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    auto userIdIdx = mariadb_bind_parameter_index(stmt, "@user_id");
    auto avatarNameIdx = mariadb_bind_parameter_index(stmt, "@avatar_name");
    auto messageIdIdx = mariadb_bind_parameter_index(stmt, "@message_id");
    auto senderNameIdx = mariadb_bind_parameter_index(stmt, "@sender_name");
    auto senderAddressIdx = mariadb_bind_parameter_index(stmt, "@sender_address");
    auto subjectIdx = mariadb_bind_parameter_index(stmt, "@subject");
    auto bodyIdx = mariadb_bind_parameter_index(stmt, "@body");
    auto oobIdx = mariadb_bind_parameter_index(stmt, "@oob");
    auto sentTimeIdx = mariadb_bind_parameter_index(stmt, "@sent_time");
    auto createdAtIdx = mailCreatedAt_.exists ? mariadb_bind_parameter_index(stmt, "@created_at") : -1;
    auto updatedAtIdx = mailUpdatedAt_.exists ? mariadb_bind_parameter_index(stmt, "@updated_at") : -1;
    auto statusIdx = mariadb_bind_parameter_index(stmt, "@status");

    std::vector<std::string> ownedStrings;

    auto avatarName = FromWideString(destAvatar.GetName());
    auto senderName = FromWideString(message.header.fromName);
    auto senderAddress = FromWideString(message.header.fromAddress);
    auto subject = FromWideString(message.header.subject);
    auto body = FromWideString(message.message);
    auto oob = FromWideString(message.oob);
    auto now = CurrentUnixTime();

    mariadb_bind_int(stmt, avatarIdIdx, destAvatar.GetAvatarId());
    mariadb_bind_int(stmt, userIdIdx, destAvatar.GetUserId());
    mariadb_bind_text(stmt, avatarNameIdx, avatarName.c_str(), -1, 0);
    mariadb_bind_int(stmt, messageIdIdx, message.header.messageId);
    mariadb_bind_text(stmt, senderNameIdx, senderName.c_str(), -1, 0);
    mariadb_bind_text(stmt, senderAddressIdx, senderAddress.c_str(), -1, 0);
    mariadb_bind_text(stmt, subjectIdx, subject.c_str(), -1, 0);
    mariadb_bind_text(stmt, bodyIdx, body.c_str(), -1, 0);
    mariadb_bind_text(stmt, oobIdx, oob.c_str(), -1, 0);
    mariadb_bind_int(stmt, sentTimeIdx, message.header.sentTime);
    BindTimestampParameter(stmt, createdAtIdx, mailCreatedAt_, now, ownedStrings);
    BindTimestampParameter(stmt, updatedAtIdx, mailUpdatedAt_, now, ownedStrings);
    mariadb_bind_int(stmt, statusIdx, static_cast<uint32_t>(message.header.status));

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        mariadb_finalize(stmt);
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
}

void WebsiteIntegrationService::EnsureUserLink(const ChatAvatar& avatar) {
    if (!enabled_) {
        return;
    }

    if (avatar.GetUserId() == 0) {
        return;
    }

    MariaDBStatement* stmt;
    auto result = mariadb_prepare(db_, userLinkSql_.c_str(), -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    auto userIdIdx = mariadb_bind_parameter_index(stmt, "@user_id");
    auto avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    auto avatarNameIdx = mariadb_bind_parameter_index(stmt, "@avatar_name");
    auto createdAtIdx = userLinkCreatedAt_.exists ? mariadb_bind_parameter_index(stmt, "@created_at") : -1;
    auto updatedAtIdx = userLinkUpdatedAt_.exists ? mariadb_bind_parameter_index(stmt, "@updated_at") : -1;

    std::vector<std::string> ownedStrings;

    auto avatarName = FromWideString(avatar.GetName());
    auto now = CurrentUnixTime();

    mariadb_bind_int(stmt, userIdIdx, avatar.GetUserId());
    mariadb_bind_int(stmt, avatarIdIdx, avatar.GetAvatarId());
    mariadb_bind_text(stmt, avatarNameIdx, avatarName.c_str(), -1, 0);
    BindTimestampParameter(stmt, createdAtIdx, userLinkCreatedAt_, now, ownedStrings);
    BindTimestampParameter(stmt, updatedAtIdx, userLinkUpdatedAt_, now, ownedStrings);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        mariadb_finalize(stmt);
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
}

void WebsiteIntegrationService::UpdateOnlineStatus(const ChatAvatar& avatar, bool isOnline) {
    if (!enabled_) {
        return;
    }

    MariaDBStatement* stmt;
    auto result = mariadb_prepare(db_, statusSql_.c_str(), -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    auto avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    auto userIdIdx = mariadb_bind_parameter_index(stmt, "@user_id");
    auto avatarNameIdx = mariadb_bind_parameter_index(stmt, "@avatar_name");
    auto onlineIdx = mariadb_bind_parameter_index(stmt, "@is_online");
    auto loginIdx = mariadb_bind_parameter_index(stmt, "@last_login");
    auto logoutIdx = mariadb_bind_parameter_index(stmt, "@last_logout");
    auto updatedIdx = statusUpdatedAt_.exists ? mariadb_bind_parameter_index(stmt, "@updated_at") : -1;
    auto createdIdx = statusCreatedAt_.exists ? mariadb_bind_parameter_index(stmt, "@created_at") : -1;

    std::vector<std::string> ownedStrings;

    auto now = CurrentUnixTime();
    auto avatarName = FromWideString(avatar.GetName());

    auto loginTime = isOnline ? now : 0u;
    auto logoutTime = isOnline ? 0u : now;

    mariadb_bind_int(stmt, avatarIdIdx, avatar.GetAvatarId());
    mariadb_bind_int(stmt, userIdIdx, avatar.GetUserId());
    mariadb_bind_text(stmt, avatarNameIdx, avatarName.c_str(), -1, 0);
    mariadb_bind_int(stmt, onlineIdx, static_cast<uint32_t>(isOnline ? 1 : 0));
    BindTimestampParameter(stmt, loginIdx, statusLoginAt_, loginTime, ownedStrings);
    BindTimestampParameter(stmt, logoutIdx, statusLogoutAt_, logoutTime, ownedStrings);
    BindTimestampParameter(stmt, updatedIdx, statusUpdatedAt_, now, ownedStrings);
    BindTimestampParameter(stmt, createdIdx, statusCreatedAt_, now, ownedStrings);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        mariadb_finalize(stmt);
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
}

WebsiteIntegrationService::ColumnInfo WebsiteIntegrationService::InspectColumn(
    const std::string& table, const std::string& column) {
    ColumnInfo info;

    if (!db_) {
        return info;
    }

    std::string sql = "SHOW COLUMNS FROM " + QuoteIdentifier(table) + " LIKE @column_name";

    MariaDBStatement* stmt;
    auto result = mariadb_prepare(db_, sql.c_str(), -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    auto columnIdx = mariadb_bind_parameter_index(stmt, "@column_name");
    if (columnIdx > 0) {
        mariadb_bind_text(stmt, columnIdx, column.c_str(), -1, 0);
    }

    result = mariadb_step(stmt);
    if (result == MARIADB_ROW) {
        info.exists = true;
        auto typePtr = mariadb_column_text(stmt, 1);
        if (typePtr) {
            info.isDateTime = ContainsDateTimeType(reinterpret_cast<const char*>(typePtr));
        }
    } else if (result != MARIADB_DONE) {
        mariadb_finalize(stmt);
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
    return info;
}

void WebsiteIntegrationService::BindTimestampParameter(MariaDBStatement* stmt, int index, const ColumnInfo& info,
    uint32_t timestamp, std::vector<std::string>& ownedStrings) const {
    if (!info.exists || index <= 0) {
        return;
    }

    if (info.isDateTime) {
        if (timestamp == 0) {
            mariadb_bind_text(stmt, index, nullptr, 0, 0);
        } else {
            ownedStrings.emplace_back(FormatDateTime(timestamp));
            mariadb_bind_text(stmt, index, ownedStrings.back().c_str(), -1, 0);
        }
    } else {
        mariadb_bind_int(stmt, index, static_cast<int>(timestamp));
    }
}

std::string WebsiteIntegrationService::FormatDateTime(uint32_t timestamp) const {
    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm tm{};

#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif

    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

uint32_t WebsiteIntegrationService::CurrentUnixTime() const {
    return static_cast<uint32_t>(std::time(nullptr));
}
