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

---

## ⚙️ Build Instructions  

Clone the repository and ensure `udplibrary/` is present in the project root.  
Install dependencies via your package manager, then build:

```bash
mkdir build && cd build
cmake ..
cmake --build .
🗄️ Database Setup
Create a new MariaDB schema + user for swgchat.

Run the initialization script:

bash
Copy code
mysql -u <user> -p < extras/init_database.sql
Edit stationchat.cfg with your DB credentials:

database_host

database_user

database_password

database_schema

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
🔒 Pro tip: Copy build/bin to a safe location before production use. Re-building will overwrite config defaults.

❤️ Support Development
If you find this project useful, consider supporting ongoing open-source development:


📄 License
This project is licensed under the MIT License.

🛰️ About
swg+ is built for the SWG emulator community to restore and extend station-style services from the original game.
It is not affiliated with Sony Online Entertainment or LucasArts.
