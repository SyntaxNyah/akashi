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
#include "lua_engine.h"

#include "aoclient.h"
#include "network/network_socket.h"
#include "server.h"

#include <QDebug>
#include <QDir>

// ── Construction / destruction ───────────────────────────────────────────────

LuaEngine::LuaEngine(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    m_L = luaL_newstate();
    luaL_openlibs(m_L);
    registerServerAPI();
}

LuaEngine::~LuaEngine()
{
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
}

// ── Script loading ───────────────────────────────────────────────────────────

void LuaEngine::loadScripts(const QString &directory)
{
    QMutexLocker lock(&m_mutex);
    QDir dir(directory);
    if (!dir.exists()) {
        qInfo() << "[Lua] Scripts directory not found:" << directory << "(skipping)";
        return;
    }

    const QStringList scripts = dir.entryList({"*.lua"}, QDir::Files, QDir::Name);
    for (const QString &script : scripts) {
        const QString path = dir.absoluteFilePath(script);
        if (luaL_dofile(m_L, path.toUtf8().constData()) != LUA_OK) {
            reportError("loading " + script);
        }
        else {
            qInfo() << "[Lua] Loaded script:" << script;
            m_active = true;
        }
    }
}

// ── Hook invocations ─────────────────────────────────────────────────────────

bool LuaEngine::callCommandHook(int uid, const QString &command, const QStringList &args)
{
    if (!m_active)
        return false;
    QMutexLocker lock(&m_mutex);

    lua_getglobal(m_L, "onCommand");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return false;
    }

    lua_pushinteger(m_L, uid);
    lua_pushstring(m_L, command.toUtf8().constData());

    // Pass args as a 1-indexed Lua table.
    lua_newtable(m_L);
    for (int i = 0; i < args.size(); ++i) {
        lua_pushstring(m_L, args[i].toUtf8().constData());
        lua_rawseti(m_L, -2, i + 1);
    }

    if (lua_pcall(m_L, 3, 1, 0) != LUA_OK) {
        reportError("onCommand");
        return false;
    }

    const bool handled = lua_toboolean(m_L, -1);
    lua_pop(m_L, 1);
    return handled;
}

void LuaEngine::callJoinHook(int uid)
{
    if (!m_active)
        return;
    QMutexLocker lock(&m_mutex);

    lua_getglobal(m_L, "onClientJoin");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return;
    }
    lua_pushinteger(m_L, uid);
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK)
        reportError("onClientJoin");
}

void LuaEngine::callLeaveHook(int uid)
{
    if (!m_active)
        return;
    QMutexLocker lock(&m_mutex);

    lua_getglobal(m_L, "onClientLeave");
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return;
    }
    lua_pushinteger(m_L, uid);
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK)
        reportError("onClientLeave");
}

// ── Internal helpers ─────────────────────────────────────────────────────────

void LuaEngine::registerServerAPI()
{
    // Store a back-pointer in the registry so static C callbacks can reach us.
    lua_pushlightuserdata(m_L, this);
    lua_setfield(m_L, LUA_REGISTRYINDEX, "_akashi_engine");

    // Build the `server` global table.
    lua_newtable(m_L);

    auto push_fn = [&](const char *name, lua_CFunction fn) {
        lua_pushlightuserdata(m_L, this);
        lua_pushcclosure(m_L, fn, 1);
        lua_setfield(m_L, -2, name);
    };

    push_fn("sendMessage",   &LuaEngine::l_sendMessage);
    push_fn("kickClient",    &LuaEngine::l_kickClient);
    push_fn("getClientName", &LuaEngine::l_getClientName);
    push_fn("getPlayerCount",&LuaEngine::l_getPlayerCount);
    push_fn("log",           &LuaEngine::l_log);

    lua_setglobal(m_L, "server");
}

void LuaEngine::reportError(const QString &context)
{
    const char *err = lua_tostring(m_L, -1);
    qWarning() << "[Lua] Error in" << context << ":" << (err ? err : "(unknown error)");
    lua_pop(m_L, 1);
}

// ── Lua-callable API ─────────────────────────────────────────────────────────

int LuaEngine::l_sendMessage(lua_State *L)
{
    auto *engine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    const int uid = static_cast<int>(luaL_checkinteger(L, 1));
    const char *msg = luaL_checkstring(L, 2);

    AOClient *client = engine->m_server->getClientByID(uid);
    if (client)
        client->sendServerMessage(QString::fromUtf8(msg));
    return 0;
}

int LuaEngine::l_kickClient(lua_State *L)
{
    auto *engine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    const int uid = static_cast<int>(luaL_checkinteger(L, 1));
    const char *reason = luaL_optstring(L, 2, "Kicked by server script");

    AOClient *client = engine->m_server->getClientByID(uid);
    if (client) {
        client->sendServerMessage(QString("You have been kicked: ") + reason);
        client->m_socket->close();
    }
    return 0;
}

int LuaEngine::l_getClientName(lua_State *L)
{
    auto *engine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    const int uid = static_cast<int>(luaL_checkinteger(L, 1));

    AOClient *client = engine->m_server->getClientByID(uid);
    if (client)
        lua_pushstring(L, client->getIpid().toUtf8().constData());
    else
        lua_pushnil(L);
    return 1;
}

int LuaEngine::l_getPlayerCount(lua_State *L)
{
    auto *engine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushinteger(L, engine->m_server->getPlayerCount());
    return 1;
}

int LuaEngine::l_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    qInfo() << "[Lua Script]" << msg;
    return 0;
}
