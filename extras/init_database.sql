CREATE DATABASE IF NOT EXISTS stationchat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE stationchat;

CREATE TABLE IF NOT EXISTS avatar (
    id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id INT UNSIGNED,
    name VARCHAR(255) NOT NULL,
    address VARCHAR(255) NOT NULL,
    attributes INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    UNIQUE KEY unique_avatar_name_address (name, address)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS room (
    id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    creator_id INT UNSIGNED NOT NULL,
    creator_name VARCHAR(255) NOT NULL,
    creator_address VARCHAR(255) NOT NULL,
    room_name VARCHAR(255) NOT NULL,
    room_topic VARCHAR(255) NOT NULL,
    room_password VARCHAR(255) NOT NULL,
    room_prefix VARCHAR(255) NOT NULL,
    room_address VARCHAR(255) NOT NULL,
    room_attributes INT UNSIGNED NOT NULL DEFAULT 0,
    room_max_size INT UNSIGNED NOT NULL DEFAULT 0,
    room_message_id INT UNSIGNED NOT NULL DEFAULT 0,
    created_at INT UNSIGNED NOT NULL DEFAULT 0,
    node_level INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    UNIQUE KEY unique_room_name_address (room_name, room_address),
    KEY idx_room_creator (creator_id),
    CONSTRAINT fk_room_creator FOREIGN KEY (creator_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS room_administrator (
    admin_avatar_id INT UNSIGNED NOT NULL,
    room_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (admin_avatar_id, room_id),
    KEY idx_room_administrator_room (room_id),
    CONSTRAINT fk_room_administrator_avatar FOREIGN KEY (admin_avatar_id) REFERENCES avatar (id) ON DELETE CASCADE,
    CONSTRAINT fk_room_administrator_room FOREIGN KEY (room_id) REFERENCES room (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS room_moderator (
    moderator_avatar_id INT UNSIGNED NOT NULL,
    room_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (moderator_avatar_id, room_id),
    KEY idx_room_moderator_room (room_id),
    CONSTRAINT fk_room_moderator_avatar FOREIGN KEY (moderator_avatar_id) REFERENCES avatar (id) ON DELETE CASCADE,
    CONSTRAINT fk_room_moderator_room FOREIGN KEY (room_id) REFERENCES room (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS room_ban (
    banned_avatar_id INT UNSIGNED NOT NULL,
    room_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (banned_avatar_id, room_id),
    KEY idx_room_ban_room (room_id),
    CONSTRAINT fk_room_ban_avatar FOREIGN KEY (banned_avatar_id) REFERENCES avatar (id) ON DELETE CASCADE,
    CONSTRAINT fk_room_ban_room FOREIGN KEY (room_id) REFERENCES room (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS room_invite (
    invited_avatar_id INT UNSIGNED NOT NULL,
    room_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (invited_avatar_id, room_id),
    KEY idx_room_invite_room (room_id),
    CONSTRAINT fk_room_invite_avatar FOREIGN KEY (invited_avatar_id) REFERENCES avatar (id) ON DELETE CASCADE,
    CONSTRAINT fk_room_invite_room FOREIGN KEY (room_id) REFERENCES room (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS persistent_message (
    id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    avatar_id INT UNSIGNED NOT NULL,
    from_name VARCHAR(255) NOT NULL,
    from_address VARCHAR(255) NOT NULL,
    subject VARCHAR(255) NOT NULL,
    sent_time INT UNSIGNED NOT NULL,
    status INT UNSIGNED NOT NULL,
    folder VARCHAR(255) NOT NULL,
    category VARCHAR(255) NOT NULL,
    message TEXT NOT NULL,
    oob LONGBLOB,
    PRIMARY KEY (id),
    KEY idx_persistent_message_avatar (avatar_id),
    CONSTRAINT fk_persistent_message_avatar FOREIGN KEY (avatar_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS friend (
    avatar_id INT UNSIGNED NOT NULL,
    friend_avatar_id INT UNSIGNED NOT NULL,
    comment TEXT,
    PRIMARY KEY (avatar_id, friend_avatar_id),
    KEY idx_friend_friend_avatar (friend_avatar_id),
    CONSTRAINT fk_friend_avatar FOREIGN KEY (avatar_id) REFERENCES avatar (id) ON DELETE CASCADE,
    CONSTRAINT fk_friend_friend_avatar FOREIGN KEY (friend_avatar_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ignore` (
    avatar_id INT UNSIGNED NOT NULL,
    ignore_avatar_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (avatar_id, ignore_avatar_id),
    KEY idx_ignore_avatar (avatar_id),
    KEY idx_ignore_ignore_avatar (ignore_avatar_id),
    CONSTRAINT fk_ignore_avatar FOREIGN KEY (avatar_id) REFERENCES avatar (id) ON DELETE CASCADE,
    CONSTRAINT fk_ignore_ignore_avatar FOREIGN KEY (ignore_avatar_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS web_user_avatar (
    user_id INT UNSIGNED NOT NULL,
    avatar_id INT UNSIGNED NOT NULL,
    avatar_name VARCHAR(255) NOT NULL,
    PRIMARY KEY (user_id, avatar_id),
    UNIQUE KEY unique_web_avatar (avatar_id),
    CONSTRAINT fk_web_user_avatar FOREIGN KEY (avatar_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS web_avatar_status (
    avatar_id INT UNSIGNED NOT NULL,
    user_id INT UNSIGNED NOT NULL,
    avatar_name VARCHAR(255) NOT NULL,
    is_online TINYINT(1) NOT NULL DEFAULT 0,
    last_login INT UNSIGNED NOT NULL DEFAULT 0,
    last_logout INT UNSIGNED NOT NULL DEFAULT 0,
    updated_at INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (avatar_id),
    KEY idx_web_avatar_status_user (user_id),
    CONSTRAINT fk_web_avatar_status_avatar FOREIGN KEY (avatar_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS web_persistent_message (
    id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    avatar_id INT UNSIGNED NOT NULL,
    user_id INT UNSIGNED NOT NULL,
    avatar_name VARCHAR(255) NOT NULL,
    message_id INT UNSIGNED NOT NULL,
    sender_name VARCHAR(255) NOT NULL,
    sender_address VARCHAR(255) NOT NULL,
    subject VARCHAR(255) NOT NULL,
    body TEXT NOT NULL,
    oob TEXT,
    sent_time INT UNSIGNED NOT NULL,
    created_at INT UNSIGNED NOT NULL,
    status INT UNSIGNED NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY unique_web_message (message_id),
    KEY idx_web_message_avatar (avatar_id),
    CONSTRAINT fk_web_message_avatar FOREIGN KEY (avatar_id) REFERENCES avatar (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
