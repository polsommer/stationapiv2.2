# swg+  

![Build](https://img.shields.io/travis/com/YOURNAME/swgplus/main?style=flat-square)  
![License](https://img.shields.io/github/license/YOURNAME/swgplus?style=flat-square)  
![C++](https://img.shields.io/badge/C++-14-blue.svg?style=flat-square)  
![MariaDB](https://img.shields.io/badge/Database-MariaDB-orange?style=flat-square)  

> A modern re-implementation of the SOE station services powering chat, login, and cross-galaxy communication for **Star Wars Galaxies** private servers.  

---

## ✨ Features  

- 🔌 **SOE-style Chat Gateway** – Mail, custom rooms, friends/ignore lists  
- 🌌 **Cross-Galaxy Communication** – Multiple servers, one community  
- 🌐 **Website Integration** – Sync presence and mail with external portals  
- 💾 **MariaDB Backend** – Persistent storage for mail, presence, and accounts  
- 🧩 **Extensible Design** – Clean, modular C++14 codebase  

---

## 📦 Requirements  

- C++14-compatible compiler  
- [Boost.Program_options](https://www.boost.org/doc/libs/release/doc/html/program_options.html)  
- [MariaDB Client Libraries](https://mariadb.com/kb/en/mariadb-client-library/)
- `udplibrary` (from the original SWG source; must be copied in)
- [Apache Ant](https://ant.apache.org/) (optional, for the legacy-compatible build wrapper)

---

## ⚙️ Build Instructions

The steps below show a complete Raspberry Pi workflow from the shell prompt the
legacy guides referenced:

```
swg@raspberrypi:~ $ sudo apt update
swg@raspberrypi:~ $ sudo apt install git ant build-essential cmake \
    libboost-program-options-dev libmariadb-dev libmariadb-dev-compat libatomic1
swg@raspberrypi:~ $ git clone https://github.com/YOURNAME/swgplus.git stationapi
swg@raspberrypi:~ $ cd stationapi
swg@raspberrypi:~/stationapi $ cp -r /path/to/original/udplibrary ./externals/
# (Result: ./externals/udplibrary/...)

# Or let the helper script pull and build everything in one go:
swg@raspberrypi:~/stationapi $ ./extras/bootstrap_build.sh

The helper clones `udplibrary` (skipping the step if it already exists), builds
the project, and installs the runtime plus default configuration files into
`/home/swg/chat`. It also drops a `stationchat` launcher alongside the `bin/`
folder, backfills any missing default configs, and attempts to link `/chat`
back to that install directory so you can start the server with:

```bash
swg@raspberrypi:~ $ cd /chat
swg@raspberrypi:/chat $ ./stationchat
```

Set the `STATIONAPI_CHAT_PREFIX` environment variable if you want the install to
land somewhere else, and `STATIONAPI_CHAT_RUNLINK` if you need the symlink target
to live somewhere other than `/chat`, e.g.

```
swg@raspberrypi:~/stationapi $ STATIONAPI_CHAT_PREFIX=$HOME/test-chat \
    STATIONAPI_CHAT_RUNLINK=$HOME/chat-link ./extras/bootstrap_build.sh
```

Once the prerequisites are in place you can build with either CMake directly or
via the Ant compatibility target described in the next section.

### Option A – CMake (native build system)

```bash
swg@raspberrypi:~/stationapi $ cmake -S . -B build
swg@raspberrypi:~/stationapi $ cmake --build build
swg@raspberrypi:~/stationapi $ cmake --install build --prefix /home/swg/chat
swg@raspberrypi:~/stationapi $ ./extras/finalize_chat_install.sh /home/swg/chat
```

> 💡 Prefer the `-S`/`-B` syntax shown above instead of running `cmake ..` from
> inside the `build` directory. It makes the intended source and binary
> directories explicit and avoids the common
> `CMake Error: The source directory "/home/swg" does not appear to contain
> CMakeLists.txt` message that appears when CMake is pointed at the wrong path.

This sequence leaves a ready-to-run layout in `/home/swg/chat` with the
`stationchat` launcher in the top-level directory, the executable itself under
`bin/`, and configuration files under `etc/stationapi/`. The launcher now
automatically switches to the install directory before starting the binary so
relative config paths resolve correctly even if you run it from somewhere else.
If the helper could create the `/chat` symlink you can start the service with
`cd /chat && ./stationchat`; otherwise run from `/home/swg/chat` or create your
own link.

### Option B – `ant compile_chat` (legacy-compatible wrapper)

For anyone migrating from the original SWG Station build scripts, the repo ships
with a small Ant wrapper that mirrors the historical `ant compile_chat`
workflow:

```bash
swg@raspberrypi:~/stationapi $ ant compile_chat
```

Under the hood this target simply calls the same `cmake -S . -B build` and
`cmake --build build` commands shown above followed by
`cmake --install build --prefix /home/swg/chat`, then runs the same
`extras/finalize_chat_install.sh` helper to drop the launcher and symlink. The
wrapper performs a quick check before invoking CMake and stops early with a
clear error if `externals/udplibrary` is missing so you know to copy the
proprietary library over before retrying. Pass `-Dinstall.prefix=/path/to/chatdir`
if you need a different install location and `-Drun.link=/path/to/link` to
override the symlink destination.

To remove build artifacts created by either approach, run:

```bash
swg@raspberrypi:~/stationapi $ ant clean
```

🗄️ Database Setup

### Provisioning MariaDB on `192.168.88.6`

The chat gateway expects a MariaDB instance that is reachable from the host
running `stationchat`. The steps below assume your database server will live on
the LAN at `192.168.88.6` (replace the address if you pick a different node).

1. **Install the server packages**

   ```bash
   sudo apt update
   sudo apt install mariadb-server mariadb-client
   ```

2. **Bind MariaDB to the LAN interface**

   Edit `/etc/mysql/mariadb.conf.d/50-server.cnf` and set `bind-address` to the
   machine’s LAN IP so remote clients (like your Raspberry Pi) can connect:

   ```ini
   [mysqld]
   bind-address = 192.168.88.6
   ```

   Restart the service after saving the file:

   ```bash
   sudo systemctl restart mariadb
   ```

3. **Secure the installation**

   Run the hardening wizard to set a root password, remove anonymous users, and
   disable the test database:

   ```bash
   sudo mysql_secure_installation
   ```

4. **Create the schema and application user**

   Log into the server as root and provision a dedicated account that is
   restricted to your game host (swap `CHAT_PASSWORD` for a strong secret):

   ```sql
   sudo mariadb
   CREATE DATABASE swgchat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
   CREATE USER 'swgchat'@'192.168.88.%' IDENTIFIED BY 'CHAT_PASSWORD';
   GRANT ALL PRIVILEGES ON swgchat.* TO 'swgchat'@'192.168.88.%';
   FLUSH PRIVILEGES;
   EXIT;
   ```

5. **Initialize the schema**

   From any machine with access (your Pi or the DB host itself), load the repo’s
   initialization script using the new account:

   ```bash
   mysql -h 192.168.88.6 -u swgchat -p swgchat < extras/init_database.sql
   ```

6. **Open the firewall (if applicable)**

   If the server uses `ufw`, allow MariaDB traffic from your LAN so the chat
   gateway can connect:

   ```bash
   sudo ufw allow from 192.168.88.0/24 to any port 3306 proto tcp
   ```

### Pointing `stationchat` at the database

Edit `etc/stationapi/stationchat.cfg` (or the copy staged in `/home/swg/chat`)
with the credentials you created above:

- `database_host = 192.168.88.6`
- `database_user = swgchat`
- `database_password = CHAT_PASSWORD`
- `database_schema = swgchat`

🌐 Website Integration (Optional)
swg+ can mirror game data into a website for community portals or account dashboards.

- `web_avatar_status` → avatar presence + timestamps
- `web_user_avatar` → avatar ↔ website user links
- `web_persistent_message` → mail mirrored from in-game

Enable by setting in `stationchat.cfg`:

```ini
website_integration_enabled = true
# Optional: configure a dedicated database connection when your website
# lives in a different schema or uses different credentials.
website_use_separate_database = true
website_database_host = 127.0.0.1
website_database_port = 3306
website_database_user = authsite
website_database_password = secret
website_database_schema = authsite
```

👉 With this bridge, your website can:

Show online players

List all characters for a user

Render mailboxes in sync with the game

### ♻️ Clustering & Load Distribution

Need to scale beyond a single gateway? Add multiple `gateway_cluster` entries to
your configuration (either on the command line or inside `stationchat.cfg`).
Each entry is expressed as `host:port[:weight]` and the local gateway is added
automatically, so you only need to list additional nodes.

```ini
# Send twice as many users to 192.168.1.12 as the other nodes
gateway_cluster = 192.168.1.10:5001
gateway_cluster = 192.168.1.11:5001
gateway_cluster = 192.168.1.12:5001:2
```

The registrar answers new login requests by walking the cluster list in a
weighted round-robin pattern, allowing you to spread connections across
multiple machines while keeping all nodes in sync through the shared database.

🚀 Running
Windows

powershell
Copy code
cd build/bin
.\Debug\stationchat.exe
Linux

bash
Copy code
cd build/bin
./stationchat

### 🥧 Raspberry Pi 4

Raspberry Pi OS (Bullseye/Bookworm) is fully supported. Install the
dependencies with:

```bash
sudo apt update
sudo apt install build-essential cmake libboost-program-options-dev \
    libmariadb-dev libmariadb-dev-compat libatomic1
```

`cmake` will automatically link against `libatomic` when it is available on
ARM targets, which resolves missing symbol errors that can appear on the Pi 4.
After installing dependencies the standard Linux build instructions shown above
apply unchanged.

#### Pi 4 Database Setup

To provision MariaDB on Raspberry Pi OS and import the default schema, run:

```bash
sudo extras/setup_pi4_database.sh
```

The helper script will:

1. Install `mariadb-server` if it is missing.
2. Start and enable the MariaDB service.
3. Create the `stationchat` schema (if needed).
4. Prompt you for an admin user + password to import `extras/init_database.sql`.

You can re-run the script safely; it will skip steps that are already complete.
🔒 Pro tip: Copy build/bin to a safe location before production use. Re-building will overwrite config defaults.

❤️ Support Development
If you find this project useful, consider supporting ongoing open-source development:


📄 License
This project is licensed under the MIT License.

🛰️ About
swg+ is built for the SWG emulator community to restore and extend station-style services from the original game.
It is not affiliated with Sony Online Entertainment or LucasArts.
