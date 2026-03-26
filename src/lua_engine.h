//////////////////////////////////////////////////////////////////////////////////////
//    akashi - a server for Attorney Online 2                                       //
//    Copyright (C) 2020  scatterflower                                             //
//                                                                                  //
//    This program is free software: you can redistribute it and/or modify          //
//    it under the terms of the GNU Affero General Public License as                //
//    published by the Free Software Foundation, either version 3 of the            //
//    License, or (at your option) any later version.                               //
//                                                                                  //
//    This program is distributed in the hope that it will be useful,               //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of                //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 //
//    GNU Affero General Public License for more details.                           //
//                                                                                  //
//    You should have received a copy of the GNU Affero General Public License      //
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.        //
//////////////////////////////////////////////////////////////////////////////////////
#ifndef LUA_ENGINE_H
#define LUA_ENGINE_H

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

class Server;

/**
 * @brief Manages a Lua 5.4 scripting engine for server-side plugins.
 *
 * Scripts are loaded from @c config/scripts/*.lua at server start.
 * Each script runs in a single shared Lua state; all calls are serialised
 * through @c m_mutex so Qt's multi-threaded packet dispatch cannot cause
 * concurrent Lua execution.
 *
 * ### Script entry points (optional — missing functions are silently skipped)
 *
 * | Lua function | When called | Return value |
 * |---|---|---|
 * | `onCommand(uid, cmd, args)` | Before C++ command handler | `true` to suppress C++ handler |
 * | `onClientJoin(uid)` | After a client completes handshake | ignored |
 * | `onClientLeave(uid)` | When a client disconnects | ignored |
 *
 * ### Server API exposed to Lua as the global `server` table
 *
 * | Lua function | Description |
 * |---|---|
 * | `server.sendMessage(uid, msg)` | Send an OOC server message to one client |
 * | `server.kickClient(uid [, reason])` | Close the client's connection |
 * | `server.getClientName(uid)` → string | Return the client's IPID (or nil) |
 * | `server.getPlayerCount()` → int | Return current connected player count |
 * | `server.log(msg)` | Write a message to the server log at INFO level |
 */
class LuaEngine : public QObject
{
    Q_OBJECT

  public:
    explicit LuaEngine(Server *server, QObject *parent = nullptr);
    ~LuaEngine();

    /**
     * @brief Loads all *.lua files found in the given directory.
     *
     * Files are executed in filesystem order. Errors are logged as warnings
     * rather than aborting the server.
     *
     * @param directory Absolute or relative path to the scripts directory.
     */
    void loadScripts(const QString &directory);

    /**
     * @brief Invokes the @c onCommand Lua hook, if defined.
     *
     * @param uid     The UID of the client running the command.
     * @param command Command name without the leading slash.
     * @param args    Argument list.
     * @return @c true if a script handled the command (C++ handler is skipped).
     */
    bool callCommandHook(int uid, const QString &command, const QStringList &args);

    /**
     * @brief Invokes the @c onClientJoin Lua hook, if defined.
     *
     * @param uid The UID of the newly connected client.
     */
    void callJoinHook(int uid);

    /**
     * @brief Invokes the @c onClientLeave Lua hook, if defined.
     *
     * @param uid The UID of the departing client.
     */
    void callLeaveHook(int uid);

    /**
     * @brief Returns @c true if at least one script was loaded successfully.
     */
    bool isActive() const { return m_active; }

  private:
    lua_State *m_L = nullptr;
    Server *m_server = nullptr;
    QMutex m_mutex;
    bool m_active = false;

    void registerServerAPI();
    void reportError(const QString &context);

    // ── Lua-callable API ────────────────────────────────────────────────────
    static int l_sendMessage(lua_State *L);
    static int l_kickClient(lua_State *L);
    static int l_getClientName(lua_State *L);
    static int l_getPlayerCount(lua_State *L);
    static int l_log(lua_State *L);
};

#endif // LUA_ENGINE_H
