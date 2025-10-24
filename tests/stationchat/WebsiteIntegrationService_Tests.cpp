#include "WebsiteIntegrationService.hpp"

#include "ChatAvatarService.hpp"
#include "FakeMariaDB.hpp"
#include "PersistentMessage.hpp"
#include "StationChatConfig.hpp"
#include "StringUtils.hpp"

#include "catch.hpp"

TEST_CASE("WebsiteIntegrationService skips work when disabled", "[stationchat]") {
    MariaDBConnection db;
    db.userLinkTableName = "web_user_avatar";
    db.onlineStatusTableName = "web_avatar_status";
    db.mailTableName = "web_persistent_message";

    StationChatConfig config;
    config.websiteIntegration.enabled = false;

    WebsiteIntegrationService service{&db, config};
    ChatAvatarService avatarService{&db};

    auto* avatar = avatarService.CreateAvatar(u"Rex", u"clone", 77, 0, u"Kamino");

    PersistentMessage message;
    message.header.avatarId = avatar->GetAvatarId();
    message.header.messageId = 12;
    message.header.fromName = u"Anakin";
    message.header.fromAddress = u"Coruscant";
    message.header.subject = u"Report";
    message.header.sentTime = 42;
    message.header.status = PersistentState::NEW;
    message.message = u"Stay alert.";

    service.RecordAvatarLogin(*avatar);
    service.RecordAvatarLogout(*avatar);
    service.RecordPersistentMessage(*avatar, message);

    CHECK(db.preparedSql.empty());
    CHECK(db.websiteUserLinks.empty());
    CHECK(db.websiteStatusRows.empty());
    CHECK(db.websiteMailRows.empty());
}

TEST_CASE("WebsiteIntegrationService records website synchronization state", "[stationchat]") {
    MariaDBConnection db;
    db.userLinkTableName = "web_user_avatar";
    db.onlineStatusTableName = "web_avatar_status";
    db.mailTableName = "web_persistent_message";

    db.columnDefinitions[db.userLinkTableName]["created_at"] = {true, false, "int"};
    db.columnDefinitions[db.userLinkTableName]["updated_at"] = {true, false, "int"};
    db.columnDefinitions[db.onlineStatusTableName]["created_at"] = {true, false, "int"};
    db.columnDefinitions[db.onlineStatusTableName]["updated_at"] = {true, false, "int"};
    db.columnDefinitions[db.onlineStatusTableName]["last_login"] = {true, false, "int"};
    db.columnDefinitions[db.onlineStatusTableName]["last_logout"] = {true, false, "int"};
    db.columnDefinitions[db.mailTableName]["created_at"] = {true, false, "int"};
    db.columnDefinitions[db.mailTableName]["updated_at"] = {true, false, "int"};

    StationChatConfig config;
    config.websiteIntegration.enabled = true;

    WebsiteIntegrationService service{&db, config};
    ChatAvatarService avatarService{&db};

    auto* avatar = avatarService.CreateAvatar(u"Hera", u"phoenix", 88, 0, u"Lothal");

    service.RecordAvatarLogin(*avatar);

    REQUIRE(db.websiteUserLinks.size() == 1);
    const auto& userLink = db.websiteUserLinks.front();
    CHECK(userLink.userId == avatar->GetUserId());
    CHECK(userLink.avatarId == avatar->GetAvatarId());
    CHECK(userLink.avatarName == FromWideString(avatar->GetName()));

    REQUIRE(db.websiteStatusRows.size() == 1);
    const auto& status = db.websiteStatusRows.front();
    CHECK(status.isOnline == 1u);
    CHECK(status.lastLogin != 0u);
    CHECK(status.lastLogout == 0u);

    service.RecordAvatarLogout(*avatar);
    REQUIRE(db.websiteStatusRows.size() == 1);
    const auto& updatedStatus = db.websiteStatusRows.front();
    CHECK(updatedStatus.isOnline == 0u);
    CHECK(updatedStatus.lastLogout != 0u);

    PersistentMessage persisted;
    persisted.header.avatarId = avatar->GetAvatarId();
    persisted.header.messageId = 33;
    persisted.header.fromName = u"Kanan";
    persisted.header.fromAddress = u"Rebels";
    persisted.header.subject = u"Meeting";
    persisted.header.sentTime = 555;
    persisted.header.status = PersistentState::UNREAD;
    persisted.message = u"May the Force be with you";

    service.RecordPersistentMessage(*avatar, persisted);

    REQUIRE(db.websiteMailRows.size() == 1);
    const auto& mail = db.websiteMailRows.front();
    CHECK(mail.avatarId == avatar->GetAvatarId());
    CHECK(mail.messageId == persisted.header.messageId);
    CHECK(mail.subject == FromWideString(persisted.header.subject));
    CHECK(mail.status == static_cast<uint32_t>(persisted.header.status));
}
