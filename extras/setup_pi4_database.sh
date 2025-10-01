#!/usr/bin/env bash
set -euo pipefail

# Ensure we are running as root (sudo).
if [[ $(id -u) -ne 0 ]]; then
    echo "This script must be run with sudo or as root." >&2
    exit 1
fi

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
INIT_SQL="${SCRIPT_DIR}/init_database.sql"

if [[ ! -f "${INIT_SQL}" ]]; then
    echo "Initialization SQL not found at ${INIT_SQL}." >&2
    exit 1
fi

# Install MariaDB server if it is not already present.
if ! dpkg -s mariadb-server >/dev/null 2>&1; then
    echo "Installing MariaDB server..."
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y mariadb-server
fi

# Ensure the service is running and enabled on boot.
if systemctl list-unit-files | grep -q '^mariadb\.service'; then
    systemctl enable mariadb
    systemctl start mariadb
else
    systemctl enable mysql
    systemctl start mysql
fi

# Prompt for credentials used to import the schema.
read -rp "MariaDB administrative user [root]: " DB_USER
DB_USER=${DB_USER:-root}

# The root account on Raspberry Pi OS uses the unix_socket plugin by default,
# so we avoid prompting for a password when the user is root.
if [[ "${DB_USER}" == "root" ]]; then
    DB_PASS=""
else
    read -rsp "Password for ${DB_USER}: " DB_PASS
    echo
fi

CREATE_DB_SQL=$'CREATE DATABASE IF NOT EXISTS stationchat\n  CHARACTER SET utf8mb4\n  COLLATE utf8mb4_unicode_ci;'

if [[ -z "${DB_PASS}" ]]; then
    echo "Ensuring stationchat database exists..."
    mysql -u "${DB_USER}" -e "${CREATE_DB_SQL}"
    echo "Importing schema from ${INIT_SQL}..."
    mysql -u "${DB_USER}" stationchat < "${INIT_SQL}"
else
    echo "Ensuring stationchat database exists..."
    mysql -u "${DB_USER}" -p"${DB_PASS}" -e "${CREATE_DB_SQL}"
    echo "Importing schema from ${INIT_SQL}..."
    mysql -u "${DB_USER}" -p"${DB_PASS}" stationchat < "${INIT_SQL}"
fi

echo "Database setup complete. Update stationchat.cfg with your connection details."
