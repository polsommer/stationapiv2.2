# StationChat codebase snapshot

## Architecture highlights
* `StationChatApp` owns both registrar and gateway nodes, normalises cluster configuration at startup, and advances each node every tick to keep networking and service logic in sync.【F:src/stationchat/StationChatApp.cpp†L5-L19】
* `StationChatConfig` centralises gateway, registrar, database, and website settings, including cluster weight deduplication so the registrar sees a clean list of endpoints.【F:src/stationchat/StationChatConfig.hpp†L36-L126】
* `GatewayNode` brings avatar, room, persistent message, and website-integration services together behind one MariaDB connection, exposing them via getters for the UDP clients.【F:src/stationchat/GatewayNode.cpp†L11-L38】
* `RegistrarNode` performs weighted round-robin selection with exponential back-off for unhealthy gateways, giving the cluster automatic failover without manual intervention.【F:src/stationchat/RegistrarNode.cpp†L31-L189】

## Strengths we can build on
* The website integration layer generates SQL tailored to the destination schema (quoting identifiers, adding timestamp columns when they exist, and supporting a dedicated database connection), which keeps external portal synchronisation flexible.【F:src/stationchat/WebsiteIntegrationService.cpp†L19-L199】
* Services cache database rows (avatars, rooms) in memory, reducing round-trips for repeat lookups and giving room/relationship logic fast access to data already fetched from MariaDB.【F:src/stationchat/ChatAvatarService.cpp†L13-L237】【F:src/stationchat/ChatRoomService.cpp†L76-L190】
* Cluster management code is already structured around reusable `GatewayClusterEndpoint` records, making it easier to extend with richer health metrics or dynamic reconfiguration later.【F:src/stationchat/StationChatConfig.hpp†L12-L125】【F:src/stationchat/RegistrarNode.cpp†L95-L189】

## Upgrade opportunities
1. **Fix broken SQL statements in the avatar service.**
   * The ignore insert lists three columns but binds only two parameters, so MariaDB rejects the query outright.【F:src/stationchat/ChatAvatarService.cpp†L113-L132】
   * `UpdateFriendComment` is misspelled as `UDPATE`, causing the statement to fail when compiled or executed.【F:src/stationchat/ChatAvatarService.cpp†L181-L206】

2. **Tidy up MariaDB resource management.**
   * Several service methods prepare statements without finalising them (`PersistFriend`, `PersistIgnore`, `RemoveFriend`, etc.), which leaks server-side resources over time.【F:src/stationchat/ChatAvatarService.cpp†L86-L206】
   * Room and relationship loaders also omit `mariadb_finalize`, so long-lived nodes risk exhausting statement handles.【F:src/stationchat/ChatAvatarService.cpp†L431-L479】【F:src/stationchat/ChatRoomService.cpp†L223-L377】
   * `PersistentMessageService::StoreMessage` and `GetMessageHeaders` have the same issue and should finalise after use.【F:src/stationchat/PersistentMessageService.cpp†L12-L114】

3. **Align SQL dialect with MariaDB.**
   * Multiple room-management helpers rely on `INSERT OR IGNORE`, a SQLite-ism that MariaDB does not understand; switching to `INSERT ... ON DUPLICATE KEY UPDATE` (or `INSERT IGNORE`) will make the statements execute as intended.【F:src/stationchat/ChatRoomService.cpp†L259-L398】
   * Room loading currently uses the `||` concatenation operator in the `LIKE` clause, which evaluates as logical OR unless the SQL mode enables pipes-as-concat. Replacing it with `CONCAT(@baseAddress, '%')` avoids depending on server-specific modes.【F:src/stationchat/ChatRoomService.cpp†L15-L73】

4. **Harden configuration defaults.**
   * The production hostname, schema, and account names are baked into `StationChatConfig`, so a default build tries to talk to the live SWG+ infrastructure. Moving these into example configuration files (and requiring explicit overrides) would prevent accidental connections with wrong credentials.【F:src/stationchat/StationChatConfig.hpp†L67-L83】

5. **Extend persistent message handling.**
   * `GetMessageHeaders` never finalises the prepared statement, which will leak, and `StoreMessage` does not clean up the insert statement either.【F:src/stationchat/PersistentMessageService.cpp†L12-L114】
   * After fetching messages, we immediately mark new mail as read; consider deferring that until the client acknowledges delivery if we need stronger reliability semantics.【F:src/stationchat/PersistentMessageService.cpp†L116-L182】

6. **Correct parameter binding bugs.**
   * `PersistBanned` looks up `@moderator_avatar_id` instead of `@banned_avatar_id`, so the bind fails and the insert never runs.【F:src/stationchat/ChatRoomService.cpp†L379-L398】

Addressing these items will stabilise day-to-day operations (by preventing SQL errors and statement leaks) and make the service safer to deploy into varied MariaDB environments.
