#include "PersistentMessageService.hpp"

#include "MariaDB.hpp"
#include "StringUtils.hpp"


PersistentMessageService::PersistentMessageService(MariaDBConnection* db)
    : db_{db} {}

PersistentMessageService::~PersistentMessageService() {}

void PersistentMessageService::StoreMessage(PersistentMessage& message) {
    MariaDBStatement* stmt;

    char sql[] = "INSERT INTO persistent_message (avatar_id, from_name, from_address, subject, "
                 "sent_time, status, "
                 "folder, category, message, oob) VALUES (@avatar_id, @from_name, @from_address, "
                 "@subject, @sent_time, @status, @folder, @category, @message, @oob)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int fromNameIdx = mariadb_bind_parameter_index(stmt, "@from_name");
    int fromAddressIdx = mariadb_bind_parameter_index(stmt, "@from_address");
    int subjectIdx = mariadb_bind_parameter_index(stmt, "@subject");
    int sentTimeIdx = mariadb_bind_parameter_index(stmt, "@sent_time");
    int statusIdx = mariadb_bind_parameter_index(stmt, "@status");
    int folderIdx = mariadb_bind_parameter_index(stmt, "@folder");
    int categoryIdx = mariadb_bind_parameter_index(stmt, "@category");
    int messageIdx = mariadb_bind_parameter_index(stmt, "@message");
    int oobIdx = mariadb_bind_parameter_index(stmt, "@oob");

    mariadb_bind_int(stmt, avatarIdIdx, message.header.avatarId);

    std::string fromName = FromWideString(message.header.fromName);
    mariadb_bind_text(stmt, fromNameIdx, fromName.c_str(), -1, 0);

    std::string fromAddress = FromWideString(message.header.fromAddress);
    mariadb_bind_text(stmt, fromAddressIdx, fromAddress.c_str(), -1, 0);

    std::string subject = FromWideString(message.header.subject);
    mariadb_bind_text(stmt, subjectIdx, subject.c_str(), -1, 0);

    mariadb_bind_int(stmt, sentTimeIdx, message.header.sentTime);
    mariadb_bind_int(stmt, statusIdx, static_cast<uint32_t>(message.header.status));

    std::string folder = FromWideString(message.header.folder);
    mariadb_bind_text(stmt, folderIdx, folder.c_str(), -1, 0);

    std::string category = FromWideString(message.header.category);
    mariadb_bind_text(stmt, categoryIdx, category.c_str(), -1, 0);

    std::string msg = FromWideString(message.message);
    mariadb_bind_text(stmt, messageIdx, msg.c_str(), -1, 0);

    mariadb_bind_blob(stmt, oobIdx, reinterpret_cast<const uint8_t*>(message.oob.data()), message.oob.size() * 2, MARIADB_STATIC);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    message.header.messageId = static_cast<uint32_t>(mariadb_last_insert_rowid(db_));
}

std::vector<PersistentHeader> PersistentMessageService::GetMessageHeaders(uint32_t avatarId) {
    std::vector<PersistentHeader> headers;
    MariaDBStatement* stmt;

    char sql[] = "SELECT id, avatar_id, from_name, from_address, subject, sent_time, status, "
                 "folder, category, message, oob FROM persistent_message WHERE avatar_id = "
                 "@avatar_id AND status IN (1, 2, 3)";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, avatarIdIdx, avatarId);

    while (mariadb_step(stmt) == MARIADB_ROW) {
        PersistentHeader header;

        header.messageId = mariadb_column_int(stmt, 0);
        header.avatarId = mariadb_column_int(stmt, 1);

        auto tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 2)));
        header.fromName = std::u16string(std::begin(tmp), std::end(tmp));

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 3)));
        header.fromAddress = std::u16string(std::begin(tmp), std::end(tmp));

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 4)));
        header.subject = std::u16string(std::begin(tmp), std::end(tmp));

        header.sentTime = mariadb_column_int(stmt, 5);
        header.status = static_cast<PersistentState>(mariadb_column_int(stmt, 6));

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 7)));
        header.folder = std::u16string(std::begin(tmp), std::end(tmp));

        tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 8)));
        header.category = std::u16string(std::begin(tmp), std::end(tmp));

        headers.push_back(std::move(header));
    }

    return headers;
}

PersistentMessage PersistentMessageService::GetPersistentMessage(
    uint32_t avatarId, uint32_t messageId) {
    MariaDBStatement* stmt;

    char sql[] = "SELECT id, avatar_id, from_name, from_address, subject, sent_time, status, "
                 "folder, category, message, oob FROM persistent_message WHERE id = @message_id "
                 "AND avatar_id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int messageIdIdx = mariadb_bind_parameter_index(stmt, "@message_id");
    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, messageIdIdx, messageId);
    mariadb_bind_int(stmt, avatarIdIdx, avatarId);

    if (mariadb_step(stmt) != MARIADB_ROW) {
        throw ChatResultException{ChatResultCode::PMSGNOTFOUND};
    }

    std::string tmp;

    PersistentMessage message;
    message.header.messageId = messageId;
    message.header.avatarId = avatarId;

    tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 2)));
    message.header.fromName = std::u16string(std::begin(tmp), std::end(tmp));

    tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 3)));
    message.header.fromAddress = std::u16string(std::begin(tmp), std::end(tmp));

    tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 4)));
    message.header.subject = std::u16string(std::begin(tmp), std::end(tmp));

    message.header.sentTime = mariadb_column_int(stmt, 5);
    message.header.status = static_cast<PersistentState>(mariadb_column_int(stmt, 6));

    tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 7)));
    message.header.folder = std::u16string(std::begin(tmp), std::end(tmp));

    tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 8)));
    message.header.category = std::u16string(std::begin(tmp), std::end(tmp));

    tmp = std::string(reinterpret_cast<const char*>(mariadb_column_text(stmt, 9)));
    message.message = std::u16string(std::begin(tmp), std::end(tmp));

    int size = mariadb_column_bytes(stmt, 10);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(mariadb_column_blob(stmt, 10));

    message.oob.resize(size / 2);
    for (int i = 0; i < size/2; ++i) {
        message.oob[i] = *reinterpret_cast<const uint16_t*>(data + i*2);
    }

    mariadb_finalize(stmt);

    if (message.header.status == PersistentState::NEW) {
        UpdateMessageStatus(
            message.header.avatarId, message.header.messageId, PersistentState::READ);
    }

    return message;
}

void PersistentMessageService::UpdateMessageStatus(
    uint32_t avatarId, uint32_t messageId, PersistentState status) {
    MariaDBStatement* stmt;

    char sql[] = "UPDATE persistent_message SET status = @status WHERE id = @message_id AND "
                 "avatar_id = @avatar_id";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int statusIdx = mariadb_bind_parameter_index(stmt, "@status");
    int messageIdIdx = mariadb_bind_parameter_index(stmt, "@message_id");
    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");

    mariadb_bind_int(stmt, statusIdx, static_cast<uint32_t>(status));
    mariadb_bind_int(stmt, messageIdIdx, messageId);
    mariadb_bind_int(stmt, avatarIdIdx, avatarId);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    mariadb_finalize(stmt);
}

void PersistentMessageService::BulkUpdateMessageStatus(
    uint32_t avatarId, const std::u16string& category, PersistentState newStatus)
{
    MariaDBStatement* stmt;

    char sql[] = "UPDATE persistent_message SET status = @status WHERE avatar_id = @avatar_id AND "
             "category = @category";

    auto result = mariadb_prepare(db_, sql, -1, &stmt, 0);
    if (result != MARIADB_OK) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }

    int statusIdx = mariadb_bind_parameter_index(stmt, "@status");
    int avatarIdIdx = mariadb_bind_parameter_index(stmt, "@avatar_id");
    int categoryIdx = mariadb_bind_parameter_index(stmt, "@category");

    mariadb_bind_int(stmt, statusIdx, static_cast<uint32_t>(newStatus));
    mariadb_bind_int(stmt, avatarIdIdx, avatarId);
    std::string cat = FromWideString(category);
    mariadb_bind_text(stmt, categoryIdx, cat.c_str(), -1, nullptr);

    result = mariadb_step(stmt);
    if (result != MARIADB_DONE) {
        throw MariaDBException{result, mariadb_errmsg(db_)};
    }
    mariadb_finalize(stmt);
}
