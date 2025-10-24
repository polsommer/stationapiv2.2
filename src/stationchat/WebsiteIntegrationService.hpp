#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class ChatAvatar;
struct PersistentMessage;
struct StationChatConfig;
struct MariaDBConnection;
struct MariaDBStatement;

class WebsiteIntegrationService {
public:
    WebsiteIntegrationService(MariaDBConnection* db, const StationChatConfig& config);
    ~WebsiteIntegrationService();

    void RecordAvatarLogin(const ChatAvatar& avatar);
    void RecordAvatarLogout(const ChatAvatar& avatar);
    void RecordPersistentMessage(const ChatAvatar& destAvatar, const PersistentMessage& message);

    bool IsEnabled() const { return enabled_; }

private:
    struct ColumnInfo {
        bool exists{false};
        bool isDateTime{false};
    };

    struct PreparedUserLinkStatement {
        MariaDBStatement* handle{nullptr};
        int userIdIdx{0};
        int avatarIdIdx{0};
        int avatarNameIdx{0};
        int createdAtIdx{0};
        int updatedAtIdx{0};
    };

    struct PreparedStatusStatement {
        MariaDBStatement* handle{nullptr};
        int avatarIdIdx{0};
        int userIdIdx{0};
        int avatarNameIdx{0};
        int isOnlineIdx{0};
        int lastLoginIdx{0};
        int lastLogoutIdx{0};
        int updatedAtIdx{0};
        int createdAtIdx{0};
    };

    struct PreparedMailStatement {
        MariaDBStatement* handle{nullptr};
        int avatarIdIdx{0};
        int userIdIdx{0};
        int avatarNameIdx{0};
        int messageIdIdx{0};
        int senderNameIdx{0};
        int senderAddressIdx{0};
        int subjectIdx{0};
        int bodyIdx{0};
        int oobIdx{0};
        int sentTimeIdx{0};
        int createdAtIdx{0};
        int updatedAtIdx{0};
        int statusIdx{0};
    };

    struct PreparedShowColumnsStatement {
        MariaDBStatement* handle{nullptr};
        int columnNameIdx{0};
    };

    void EnsureUserLink(const ChatAvatar& avatar);
    void UpdateOnlineStatus(const ChatAvatar& avatar, bool isOnline);
    ColumnInfo InspectColumn(const std::string& table, const std::string& column);
    void BindTimestampParameter(
        MariaDBStatement* stmt, int index, const ColumnInfo& info, uint32_t timestamp, std::vector<std::string>& ownedStrings) const;
    std::string FormatDateTime(uint32_t timestamp) const;
    uint32_t CurrentUnixTime() const;

    MariaDBConnection* db_;
    bool ownsDatabase_{false};
    bool enabled_{false};
    std::string userLinkTable_;
    std::string onlineStatusTable_;
    std::string mailTable_;
    std::string userLinkSql_;
    std::string statusSql_;
    std::string mailSql_;
    ColumnInfo userLinkCreatedAt_;
    ColumnInfo userLinkUpdatedAt_;
    ColumnInfo statusCreatedAt_;
    ColumnInfo statusUpdatedAt_;
    ColumnInfo statusLoginAt_;
    ColumnInfo statusLogoutAt_;
    ColumnInfo mailCreatedAt_;
    ColumnInfo mailUpdatedAt_;
    PreparedUserLinkStatement userLinkStmt_;
    PreparedStatusStatement statusStmt_;
    PreparedMailStatement mailStmt_;
    std::unordered_map<std::string, PreparedShowColumnsStatement> showColumnsStatements_;
};
