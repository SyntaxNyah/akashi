# Akashi

## Project Overview

**Akashi** is a C++ server for **Attorney Online 2 (AO2)**, a visual-novel-style online roleplay game based on the Ace Attorney franchise. The server handles real-time multi-user communication, character selection, area management, moderation, music playback, and testimony recording, all over the AO2 network protocol.

- **Language:** C++20
- **Version:** 1.9 (codename "jackfruit")
- **License:** GNU Affero General Public License v3.0 (AGPL-3.0)
- **Original author:** scatterflower (2020)
- **Wiki:** https://github.com/AttorneyOnline/akashi/wiki

## Architecture

### Tech Stack

| Component | Version / Details |
|-----------|------------------|
| C++ Standard | C++20 |
| CMake | >= 3.19 |
| Qt | 6.4+ (CI uses 6.5.3) |
| Qt modules | Core, Network, WebSockets, Sql |
| Database | SQLite via Qt Sql (`config/akashi.db`) |
| Code formatting | clang-format 14 (enforced by CI) |
| Documentation | Doxygen |

### Folder Structure

```
akashi/
├── CMakeLists.txt
├── Dockerfile              # Multi-stage Docker build (Ubuntu Noble)
├── docker-compose.yml      # Port 27016, bind-mounts ./config
├── docker-entrypoint.sh    # Copies sample config on first run
├── Doxyfile
├── .clang-format           # 4-space indent, Allman-style braces
├── .github/workflows/main.yml  # CI: format check → Linux build → Windows build
├── bin/
│   └── config_sample/      # Sample configuration files shipped with binary
│       ├── config.ini
│       ├── acl_roles.ini
│       ├── areas.ini
│       ├── ambience.ini
│       ├── backgrounds.txt
│       ├── characters.txt
│       ├── command_extensions.ini
│       ├── discord.ini
│       ├── ipbans.json
│       ├── music.json
│       └── text/
│           ├── 8ball.txt
│           ├── autorp.json
│           ├── commandhelp.json
│           ├── filter.txt
│           ├── gimp.txt
│           ├── logtext.ini
│           ├── praise.txt
│           └── reprimands.txt
├── data/
│   └── icon/               # Windows .rc icon resource
├── scripts/
│   └── maxmind2sqlite.ps1  # Import MaxMind ASN CSV → SQLite
└── src/
    ├── main.cpp
    ├── server.cpp / server.h
    ├── aoclient.cpp / aoclient.h
    ├── area_data.cpp / area_data.h
    ├── config_manager.cpp / config_manager.h
    ├── db_manager.cpp / db_manager.h
    ├── acl_roles_handler.cpp / acl_roles_handler.h
    ├── discord.cpp / discord.h
    ├── music_manager.cpp / music_manager.h
    ├── medieval_parser.cpp / medieval_parser.h
    ├── serverpublisher.cpp / serverpublisher.h
    ├── testimony_recorder.cpp
    ├── command_extension.cpp / command_extension.h
    ├── packets.cpp
    ├── akashiutils.h
    ├── crypto_helper.h
    ├── data_types.h
    ├── typedefs.h
    ├── commands/           # Command handlers (one file per domain)
    │   ├── area.cpp
    │   ├── authentication.cpp
    │   ├── casing.cpp
    │   ├── command_helper.cpp
    │   ├── messaging.cpp
    │   ├── moderation.cpp
    │   ├── music.cpp
    │   └── roleplay.cpp
    ├── logger/
    │   ├── u_logger.cpp / u_logger.h
    │   ├── writer_full.cpp / writer_full.h
    │   └── writer_modcall.cpp / writer_modcall.h
    ├── network/
    │   ├── aopacket.cpp / aopacket.h
    │   └── network_socket.cpp / network_socket.h
    └── packet/             # One class per AO2 packet type
        ├── packet_factory.cpp / packet_factory.h
        ├── packet_generic.cpp / packet_generic.h
        └── packet_<NAME>.cpp / packet_<NAME>.h
            (askchaa, casea, cc, ch, ct, de, ee, hi, hp, id,
             ma, mc, ms, pe, pr, pw, rc, rd, rm, rt, setcase, zz)
```

### Key Components

| Class | File | Role |
|-------|------|------|
| `Server` | `src/server.{cpp,h}` | Top-level QObject; owns `QWebSocketServer`, manages all `AOClient` instances, areas, database, logger, Discord, music manager, and server publisher |
| `AOClient` | `src/aoclient.{cpp,h}` | Represents one connected client. Owns the `NetworkSocket`. Exposes static `COMMANDS` map for all `/command` processing |
| `NetworkSocket` | `src/network/network_socket.{cpp,h}` | Wraps `QWebSocket`. Decodes text frames into `AOPacket*` and emits `handlePacket` signal |
| `AOPacket` | `src/network/aopacket.{cpp,h}` | Abstract base for all packets. Handles AO2 escape/unescape and `%` terminator. Concrete types override `getPacketInfo()` and `handlePacket()` |
| `PacketFactory` | `src/packet/packet_factory.{cpp,h}` | Static factory registry. Maps packet header strings to constructors via `std::map`. Packets self-register via `AOPacket::registerPackets()` |
| `AreaData` | `src/area_data.{cpp,h}` | One logical room. Tracks players, status, evidence, locks, testimony recorder, timers, jukebox |
| `ConfigManager` | `src/config_manager.{cpp,h}` | Static-only. Reads all INI/JSON config files via `QSettings`. Validates at startup. `CONFIG_VERSION 1` |
| `DBManager` | `src/db_manager.{cpp,h}` | SQLite wrapper (`akashi.db`, `DB_VERSION 2`). Ban records and moderator accounts |
| `ACLRolesHandler` | `src/acl_roles_handler.{cpp,h}` | Role-based permission system. `ACLRole::Permission` is a bitmask enum: `KICK`, `BAN`, `BGLOCK`, `MODIFY_USERS`, `CM`, `MUTE`, `ANNOUNCE`, `MODCHAT`, `SUPER` (0xffffffff), etc. |
| `ULogger` | `src/logger/u_logger.{cpp,h}` | Universal logger. Dispatches to `WriterFull` (daily files) or `WriterModcall` (buffer-only, dumps on modcall) |
| `Discord` | `src/discord.{cpp,h}` | HTTP POST webhook to Discord for modcall and ban events |
| `ServerPublisher` | `src/serverpublisher.{cpp,h}` | Periodically POSTs server info to AO2 master server |
| `MedievalParser` | `src/medieval_parser.{cpp,h}` | TF2 medieval speech filter port. Data: `config/text/autorp.json` |

### Data Flow

1. `QWebSocketServer` accepts a new WebSocket connection.
2. A `NetworkSocket` wraps the `QWebSocket`. On a text frame, it splits on `%` terminators, delegates each to `PacketFactory::createPacket()`, and emits `handlePacket(AOPacket*)`.
3. `AOClient` receives the signal and calls `AOPacket::handlePacket(area, *this)`.
4. The concrete packet class validates against `PacketInfo::min_args` and `PacketInfo::acl_permission`, then executes game logic.
5. Responses are written back via `NetworkSocket::write(AOPacket*)` — serialized as `HEADER#field1#field2#...%` in UTF-8 over WebSocket.
6. OOC commands (e.g. `/kick`) are handled by `PacketCT`, which routes to `AOClient::COMMANDS` → `src/commands/*.cpp` handler methods.

## Development Setup

### Prerequisites

- **CMake** >= 3.19
- **Qt** >= 6.4 with modules: `Core`, `Network`, `WebSockets`, `Sql`
- C++20-capable compiler: GCC (Linux) or MSVC 2019+ (Windows)
- **clang-format** version 14 (for format checking/applying)

### Build — Linux

```bash
cmake .
cmake --build . --config Release
# Binary output: bin/akashi
```

### Build — Windows (MSVC)

```powershell
cmake . -D CMAKE_BUILD_TYPE=Release
cmake --build . --config Release
windeployqt bin\akashi.exe --release --no-translations --no-compiler-runtime --no-opengl-sw
# Binary output: bin\akashi.exe
```

### Running

Place a `config/` directory alongside the binary (copied from `bin/config_sample/`), then:

```bash
./bin/akashi
```

The server validates `config/config.ini` at startup and exits immediately if invalid.

## Docker Setup

```bash
# Start the server (./config is auto-populated on first run)
docker compose up -d
```

`docker-compose.yml` exposes port `27016` and bind-mounts `./config:/app/config`. `docker-entrypoint.sh` copies `config_sample/` into the empty `config/` volume on first run.

**Note:** The Dockerfile currently uses Qt5 via `qmake`, which differs from the CMake + Qt6 path used in CI — this is a known discrepancy.

## Configuration

All config files live in `config/` relative to the binary. Sample files in `bin/config_sample/`.

### config/config.ini — Key Settings

**[Options]**

| Key | Default | Description |
|-----|---------|-------------|
| `max_players` | 100 | Maximum simultaneous connections |
| `port` | 27016 | WebSocket listen port |
| `server_name` | "An Unnamed Server" | Shown on master server list |
| `motd` | "MOTD is not set." | Sent in OOC to joining users |
| `auth` | `simple` | Auth mode: `simple` or `advanced` |
| `modpass` | `changeme` | Moderator password (simple auth — **change this**) |
| `logbuffer` | 500 | Circular area log buffer size |
| `logging` | `modcall` | Log mode: `modcall`, `full`, or `fullarea` |
| `maximum_statements` | 10 | Max testimony recorder statements |
| `multiclient_limit` | 15 | Max connections per IP |
| `maximum_characters` | 256 | Max IC/OOC message length |
| `message_floodguard` | 250 | Per-area rate limit (ms) |
| `packet_rate_limit_soft` | 10 | Packets/s before warning |
| `packet_rate_limit_hard` | 20 | Packets/s before disconnect |
| `afk_timeout` | 300 | Seconds before AFK flag |
| `asset_url` | http://attorneyoffline.de/base/ | Sent to clients for content downloads |

**[Advertiser]**

| Key | Default | Description |
|-----|---------|-------------|
| `advertise` | true | Publish to master server |
| `ms_ip` | https://servers.aceattorneyonline.com/servers | Master server URL |
| `cloudflare_enabled` | false | Override WebAO port to 80 for Cloudflare tunnels |

### config/acl_roles.ini

Defines named moderator roles. Each INI section is a role name; keys are permission names set to `true`/`false`.

Available permissions: `kick`, `ban`, `bglock`, `modify_users`, `cm`, `global_timer`, `evi_mod`, `motd`, `announce`, `modchat`, `mute`, `uncm`, `savetest`, `force_charselect`, `bypass_locks`, `ignore_bglist`, `send_notice`, `jukebox`.

### config/areas.ini

Defines areas in `[INDEX:Name]` sections.

| Key | Description |
|-----|-------------|
| `background` | Default background name |
| `protected_area` | Requires CM/permission to enter |
| `iniswap_allowed` | Allow character ini-swapping |
| `evidence_mod` | `ffa`, `cm`, `hidden_cm` — who can modify evidence |
| `blankposting_allowed` | Allow empty IC messages |
| `force_immediate` | Force non-interrupting pre-animations |

### config/text/

| File | Purpose |
|------|---------|
| `commandhelp.json` | Help text served to clients for each `/command` |
| `autorp.json` | Word replacement data for medieval speech mode |
| `8ball.txt` | Responses for `/8ball` |
| `gimp.txt` | Random replacement messages for gimped users |
| `filter.txt` | Word filter list |
| `logtext.ini` | Template strings for log output formatting |

### Runtime Database

`config/akashi.db` (SQLite, auto-created on first run). Stores ban records and moderator accounts. Schema version: `DB_VERSION 2`.

## Common Development Tasks

### Format Check (required by CI)

```bash
clang-format --dry-run --Werror src/**/*.cpp src/**/*.h
```

### Apply Formatting

```bash
clang-format -i src/**/*.cpp src/**/*.h
```

### Generate API Docs

```bash
doxygen Doxyfile
```

### Adding a New Packet Type

1. Create `src/packet/packet_<name>.h` and `.cpp`, inherit from `AOPacket`.
2. Implement `getPacketInfo()` returning `PacketInfo` with `header`, `min_args`, `acl_permission`.
3. Implement `handlePacket(AreaData *area, AOClient &client) const`.
4. Register in `AOPacket::registerPackets()` (`src/packets.cpp`): `PacketFactory::registerClass<PacketFoo>("FOO")`.
5. Add both files to `qt_add_executable(akashi ...)` in `CMakeLists.txt`.

### Adding a New Server Command

1. Implement `void AOClient::cmdFoo(int argc, QStringList argv)` in `src/commands/*.cpp`.
2. Declare in `src/aoclient.h`.
3. Add to `AOClient::COMMANDS` map in `src/aoclient.cpp`:
   ```cpp
   {"foo", {{{ACLRole::Permission::NONE}, 1, &AOClient::cmdFoo}}}
   ```
4. Add help text to `bin/config_sample/text/commandhelp.json`.

## Code Style and Naming Conventions

### `.clang-format` Rules

- **Indent width:** 4 spaces
- **Brace style:** Allman — opening braces on new lines for functions, classes, `if/else`, etc.
- **Column limit:** None (`ColumnLimit: 0`)
- **Short functions:** Allowed on single line
- **Constructor initializers:** Break after colon

### Naming Conventions

| Construct | Convention | Example |
|-----------|-----------|---------|
| Classes | `PascalCase` | `AOClient`, `AreaData`, `PacketFactory` |
| Public methods | `camelCase` | `getPacketInfo()`, `handlePacket()` |
| Command methods | `cmd` prefix + `camelCase` | `cmdKick()`, `cmdGetArea()` |
| Member variables | `m_` prefix + `snake_case` | `m_content`, `m_client_socket` |
| Parameters | `f_` prefix + `snake_case` | `f_packet`, `f_area_name` |
| Local variables | `snake_case` | `raw_packet`, `class_map` |
| Constants / enum values | `UPPER_SNAKE_CASE` | `KICK`, `BAN`, `SUPER`, `IDLE` |
| Packet headers | Uppercase 2-3 letter codes | `"MS"`, `"HI"`, `"CT"` |

### Other Conventions

- All `.cpp`/`.h` files begin with the AGPL-3.0 license header block.
- Classes using Qt signals/slots inherit from `QObject` and include `Q_OBJECT`.
- `Q_GADGET` / `Q_ENUM` used for enums serializable through `QVariant`.
- Static-only utility classes use a private constructor (`AkashiUtils`, `ConfigManager`).
- Doxygen `/** ... */` comments on all public API declarations; `//!<` for inline member docs.

## Testing

There is no dedicated test suite. Quality assurance is handled by:
- **CI format check** — clang-format 14 enforced on every push/PR
- **CI build** — both Linux (GCC) and Windows (MSVC) must compile successfully
- **Manual testing** — run the server against a real AO2 client; check `logs/` and `config/akashi.db`

## Project-Specific Notes

### AO2 Network Protocol

- **Packet format:** `HEADER#field1#field2#...%` — `%` is the terminator, `#` separates fields.
- **Escape codes:** `#`→`<pound>`, `%`→`<percent>`, `$`→`<dollar>`, `&`→`<and>`.
- Protocol docs: https://github.com/AttorneyOnline/docs/blob/master/docs/development/network.md

### Key Packet Types

| Header | Purpose |
|--------|---------|
| `HI` | Client handshake (sends HDID) |
| `ID` | Client software info |
| `MS` | In-character (IC) message — largest/most complex packet |
| `CT` | OOC message / commands |
| `MC` | Music change |
| `RT` | WT/CE shouts |
| `HP` | Health bar update |
| `PE`/`EE`/`DE` | Add/edit/delete evidence |
| `ZZ` | Mod call |
| `CH` | Keep-alive ping |

### Authentication Modes

- **Simple:** Single shared `modpass`. Login with `/login <modpass>`.
- **Advanced:** Per-user accounts in `akashi.db` with roles from `acl_roles.ini`. Password complexity enforced by `[Password]` section.

### IPID System

Each client is assigned an IPID — a hashed representation of their IP (via `crypto_helper.h`) — used for moderation that persists across reconnections without exposing raw IPs.

### Area Management

Areas are defined statically in `config/areas.ini` and are not created/destroyed at runtime. Each area maintains independent state: players, evidence, status, CM list, jukebox queue, timers, locks, testimony recorder, and mod log buffer.

### Medieval Mode

`MedievalParser` reimplements the `tf_autorp` medieval speech filter from TF2's Source SDK 2013. Word replacement data lives in `config/text/autorp.json`. Per the source: do not report bugs without confirming they also exist in TF2.

### Windows CI Artifact

The Windows CI job runs `windeployqt` and uploads the result as the `akashi-windows` artifact — a self-contained zip with all required Qt DLLs.
