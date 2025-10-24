#include "PersistentMessageService.hpp"

#include "FakeMariaDB.hpp"
#include "StringUtils.hpp"

#include "catch.hpp"

TEST_CASE("PersistentMessageService stores UTF-8 messages and updates status", "[stationchat]") {
    MariaDBConnection db;
    PersistentMessageService service{&db};

    PersistentMessage message;
    message.header.avatarId = 7;
    message.header.fromName = u"Brontë";
    message.header.fromAddress = u"ユニット";
    message.header.subject = u"Invitation – déjà vu";
    message.header.sentTime = 123456;
    message.header.status = PersistentState::NEW;
    message.header.folder = u"Inbox";
    message.header.category = u"General";
    message.message = u"こんにちは";
    message.oob = u"☃";

    service.StoreMessage(message);

    REQUIRE(message.header.messageId != 0);
    REQUIRE(db.persistentMessages.size() == 1);

    const auto& storedRow = db.persistentMessages.front();
    CHECK(storedRow.avatarId == message.header.avatarId);
    CHECK(ToWideString(storedRow.fromName) == message.header.fromName);
    CHECK(ToWideString(storedRow.subject) == message.header.subject);
    CHECK(storedRow.status == static_cast<uint32_t>(PersistentState::NEW));

    auto headers = service.GetMessageHeaders(message.header.avatarId);
    REQUIRE(headers.size() == 1);
    CHECK(headers.front().messageId == message.header.messageId);
    CHECK(headers.front().subject == message.header.subject);

    auto persisted = service.GetPersistentMessage(message.header.avatarId, message.header.messageId);
    CHECK(persisted.header.messageId == message.header.messageId);
    CHECK(persisted.header.status == PersistentState::READ);
    CHECK(persisted.message == message.message);
    CHECK(persisted.oob == message.oob);
    CHECK(db.persistentMessages.front().status == static_cast<uint32_t>(PersistentState::READ));

    service.BulkUpdateMessageStatus(message.header.avatarId, message.header.category, PersistentState::TRASH);
    CHECK(db.persistentMessages.front().status == static_cast<uint32_t>(PersistentState::TRASH));
}
