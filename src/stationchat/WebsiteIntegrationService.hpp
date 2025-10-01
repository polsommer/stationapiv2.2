#pragma once

#include <cstdint>
#include <string>
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
};
