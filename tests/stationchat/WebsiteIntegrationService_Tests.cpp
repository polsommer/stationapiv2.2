#include "WebsiteIntegrationService.hpp"

#include "FakeMariaDB.hpp"
#include "PersistentMessage.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include "catch.hpp"

#define private public
#include "stationchat/ChatAvatar.hpp"
#undef private

namespace {

StationChatConfig MakeConfig() {
    StationChatConfig config;
    config.websiteIntegration.enabled = true;
    config.websiteIntegration.userLinkTable = "web_user_avatar";
    config.websiteIntegration.onlineStatusTable = "web_avatar_status";
    config.websiteIntegration.mailTable = "web_persistent_message";
    return config;
}

void ConfigureColumn(MariaDBConnection& db, const std::string& table, const std::string& column, bool isDateTime) {
    FakeColumnDefinition def;
    def.typeName = isDateTime ? "datetime" : "int";
    def.isDateTime = isDateTime;
    db.tableColumns[table][column] = std::move(def);
}

std::string Narrow(const std::u16string& value) {
    return FromWideString(value);
}

ChatAvatar MakeAvatar(uint32_t avatarId, uint32_t userId, const std::u16string& name) {
    ChatAvatar avatar{nullptr};
    avatar.avatarId_ = avatarId;
    avatar.userId_ = userId;
    avatar.name_ = name;
    return avatar;
}

} // namespace

TEST_CASE("Website integration upserts login and status records", "[stationchat][website]") {
    MariaDBConnection db;
    auto config = MakeConfig();

    db.userLinkTableName = config.websiteIntegration.userLinkTable;
    db.statusTableName = config.websiteIntegration.onlineStatusTable;
    db.mailTableName = config.websiteIntegration.mailTable;

    ConfigureColumn(db, db.userLinkTableName, "created_at", true);
    ConfigureColumn(db, db.userLinkTableName, "updated_at", true);
    ConfigureColumn(db, db.statusTableName, "created_at", true);
    ConfigureColumn(db, db.statusTableName, "updated_at", true);
    ConfigureColumn(db, db.statusTableName, "last_login", true);
    ConfigureColumn(db, db.statusTableName, "last_logout", true);
    ConfigureColumn(db, db.mailTableName, "created_at", true);
    ConfigureColumn(db, db.mailTableName, "updated_at", true);

    WebsiteIntegrationService service{&db, config};
    REQUIRE(service.IsEnabled());

    auto avatar = MakeAvatar(42, 777, u"Integration Tester");

    service.RecordAvatarLogin(avatar);

    auto& linkRows = db.userLinkRows[db.userLinkTableName];
    REQUIRE(linkRows.size() == 1);
    const auto& link = linkRows.front();
    CHECK(link.avatarId == avatar.GetAvatarId());
    CHECK(link.userId == avatar.GetUserId());
    CHECK(link.avatarName == Narrow(avatar.GetName()));
    CHECK_FALSE(link.createdAt.isNull);
    CHECK_FALSE(link.updatedAt.isNull);

    auto& statusRows = db.statusRows[db.statusTableName];
    REQUIRE(statusRows.size() == 1);
    const auto& status = statusRows.front();
    CHECK(status.avatarId == avatar.GetAvatarId());
    CHECK(status.isOnline);
    CHECK_FALSE(status.lastLogin.isNull);
    CHECK(status.lastLogout.isNull);
    auto loginTimestamp = status.lastLogin.textValue;

    service.RecordAvatarLogout(avatar);

    REQUIRE(statusRows.size() == 1);
    const auto& updatedStatus = statusRows.front();
    CHECK_FALSE(updatedStatus.isOnline);
    CHECK_FALSE(updatedStatus.lastLogout.isNull);
    CHECK(updatedStatus.lastLogin.textValue == loginTimestamp);

    PersistentMessage message;
    message.header.messageId = 9001;
    message.header.fromName = u"Sender";
    message.header.fromAddress = u"sender@example.com";
    message.header.subject = u"Integration Test";
    message.header.sentTime = 12345;
    message.header.status = PersistentState::UNREAD;
    message.message = u"Body";
    message.oob = u"OOB";

    service.RecordPersistentMessage(avatar, message);

    auto& mailRows = db.mailRows[db.mailTableName];
    REQUIRE(mailRows.size() == 1);
    const auto& mail = mailRows.front();
    CHECK(mail.avatarId == avatar.GetAvatarId());
    CHECK(mail.userId == avatar.GetUserId());
    CHECK(mail.messageId == message.header.messageId);
    CHECK(mail.senderName == Narrow(message.header.fromName));
    CHECK(mail.senderAddress == Narrow(message.header.fromAddress));
    CHECK(mail.subject == Narrow(message.header.subject));
    CHECK(mail.body == Narrow(message.message));
    CHECK(mail.oob == Narrow(message.oob));
    CHECK(mail.sentTime == message.header.sentTime);
    CHECK(mail.status == static_cast<uint32_t>(message.header.status));
    CHECK_FALSE(mail.createdAt.isNull);
    CHECK_FALSE(mail.updatedAt.isNull);

    REQUIRE(linkRows.size() == 1);
}

TEST_CASE("Website integration caches SHOW COLUMNS statements", "[stationchat][website]") {
    MariaDBConnection db;
    auto config = MakeConfig();

    db.userLinkTableName = config.websiteIntegration.userLinkTable;
    db.statusTableName = config.websiteIntegration.onlineStatusTable;
    db.mailTableName = config.websiteIntegration.mailTable;

    ConfigureColumn(db, db.userLinkTableName, "created_at", true);
    ConfigureColumn(db, db.userLinkTableName, "updated_at", true);
    ConfigureColumn(db, db.statusTableName, "created_at", true);
    ConfigureColumn(db, db.statusTableName, "updated_at", true);
    ConfigureColumn(db, db.statusTableName, "last_login", true);
    ConfigureColumn(db, db.statusTableName, "last_logout", true);
    ConfigureColumn(db, db.mailTableName, "created_at", true);
    ConfigureColumn(db, db.mailTableName, "updated_at", true);

    WebsiteIntegrationService service{&db, config};
    REQUIRE(service.IsEnabled());

    const auto userLinkSql = std::string{"SHOW COLUMNS FROM `"} + db.userLinkTableName + "` LIKE @column_name";
    const auto statusSql = std::string{"SHOW COLUMNS FROM `"} + db.statusTableName + "` LIKE @column_name";
    const auto mailSql = std::string{"SHOW COLUMNS FROM `"} + db.mailTableName + "` LIKE @column_name";

    CHECK(db.preparedStatementCount[userLinkSql] == 1);
    CHECK(db.preparedStatementCount[statusSql] == 1);
    CHECK(db.preparedStatementCount[mailSql] == 1);

    auto avatar = MakeAvatar(7, 99, u"Cache Test");
    service.RecordAvatarLogin(avatar);
    service.RecordAvatarLogout(avatar);

    PersistentMessage message;
    message.header.messageId = 1;
    message.header.status = PersistentState::UNREAD;
    service.RecordPersistentMessage(avatar, message);

    CHECK(db.preparedStatementCount[userLinkSql] == 1);
    CHECK(db.preparedStatementCount[statusSql] == 1);
    CHECK(db.preparedStatementCount[mailSql] == 1);
}
