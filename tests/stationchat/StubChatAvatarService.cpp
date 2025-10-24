#include "ChatAvatarService.hpp"

#include <algorithm>
#include <memory>

namespace {
uint32_t NextAvatarId() {
    static uint32_t nextId = 1;
    return nextId++;
}
}

ChatAvatarService::ChatAvatarService(MariaDBConnection* db)
    : db_{db} {}

ChatAvatarService::~ChatAvatarService() = default;

ChatAvatar* ChatAvatarService::GetAvatar(const std::u16string& name, const std::u16string& address) {
    auto it = std::find_if(std::begin(avatarCache_), std::end(avatarCache_),
        [&](const auto& avatar) { return avatar->GetName() == name && avatar->GetAddress() == address; });
    if (it == std::end(avatarCache_)) {
        return nullptr;
    }
    return it->get();
}

ChatAvatar* ChatAvatarService::GetAvatar(uint32_t avatarId) {
    auto it = std::find_if(std::begin(avatarCache_), std::end(avatarCache_),
        [avatarId](const auto& avatar) { return avatar->GetAvatarId() == avatarId; });
    if (it == std::end(avatarCache_)) {
        return nullptr;
    }
    return it->get();
}

ChatAvatar* ChatAvatarService::CreateAvatar(const std::u16string& name, const std::u16string& address, uint32_t userId,
    uint32_t loginAttributes, const std::u16string& loginLocation) {
    auto avatar = std::make_unique<ChatAvatar>(this, name, address, userId, loginAttributes, loginLocation);
    avatar->avatarId_ = NextAvatarId();
    auto* result = avatar.get();
    avatarCache_.push_back(std::move(avatar));
    return result;
}

void ChatAvatarService::DestroyAvatar(ChatAvatar* avatar) {
    if (!avatar) {
        return;
    }

    avatarCache_.erase(std::remove_if(std::begin(avatarCache_), std::end(avatarCache_),
                               [avatar](const auto& candidate) { return candidate.get() == avatar; }),
        std::end(avatarCache_));
}

void ChatAvatarService::LoginAvatar(ChatAvatar* avatar) {
    if (!avatar) {
        return;
    }

    if (!IsOnline(avatar)) {
        onlineAvatars_.push_back(avatar);
    }
    avatar->isOnline_ = true;
}

void ChatAvatarService::LogoutAvatar(ChatAvatar* avatar) {
    if (!avatar) {
        return;
    }

    onlineAvatars_.erase(std::remove(std::begin(onlineAvatars_), std::end(onlineAvatars_), avatar),
        std::end(onlineAvatars_));
    avatar->isOnline_ = false;
}

void ChatAvatarService::PersistAvatar(const ChatAvatar*) {}

void ChatAvatarService::PersistFriend(uint32_t, uint32_t, const std::u16string&) {}

void ChatAvatarService::PersistIgnore(uint32_t, uint32_t) {}

void ChatAvatarService::RemoveFriend(uint32_t, uint32_t) {}

void ChatAvatarService::RemoveIgnore(uint32_t, uint32_t) {}

void ChatAvatarService::UpdateFriendComment(uint32_t, uint32_t, const std::u16string&) {}

bool ChatAvatarService::IsOnline(const ChatAvatar* avatar) const {
    return std::find(std::begin(onlineAvatars_), std::end(onlineAvatars_), avatar) != std::end(onlineAvatars_);
}
