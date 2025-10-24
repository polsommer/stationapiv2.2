#include "ChatAvatarService.hpp"

ChatAvatarService::ChatAvatarService(MariaDBConnection* db)
    : db_{db} {}

ChatAvatarService::~ChatAvatarService() = default;

ChatAvatar* ChatAvatarService::GetAvatar(const std::u16string&, const std::u16string&) { return nullptr; }

ChatAvatar* ChatAvatarService::GetAvatar(uint32_t) { return nullptr; }

ChatAvatar* ChatAvatarService::CreateAvatar(const std::u16string&, const std::u16string&, uint32_t, uint32_t,
    const std::u16string&) {
    return nullptr;
}

void ChatAvatarService::DestroyAvatar(ChatAvatar*) {}

void ChatAvatarService::LoginAvatar(ChatAvatar*) {}

void ChatAvatarService::LogoutAvatar(ChatAvatar*) {}

void ChatAvatarService::PersistAvatar(const ChatAvatar*) {}

void ChatAvatarService::PersistFriend(uint32_t, uint32_t, const std::u16string&) {}

void ChatAvatarService::PersistIgnore(uint32_t, uint32_t) {}

void ChatAvatarService::RemoveFriend(uint32_t, uint32_t) {}

void ChatAvatarService::RemoveIgnore(uint32_t, uint32_t) {}

void ChatAvatarService::UpdateFriendComment(uint32_t, uint32_t, const std::u16string&) {}

