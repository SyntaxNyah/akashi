-- example.lua — sample Akashi server script
--
-- Place *.lua files in config/scripts/ to have them loaded at server start.
-- All scripts share a single Lua state and run under a mutex — they are safe
-- from concurrent execution but must not block for long periods.
--
-- Available entry points:
--   onCommand(uid, cmd, args)  → return true to suppress the C++ handler
--   onClientJoin(uid)
--   onClientLeave(uid)
--
-- Available server API (global table `server`):
--   server.sendMessage(uid, msg)          send an OOC message to one client
--   server.kickClient(uid [, reason])     close a client's connection
--   server.getClientName(uid) → string    return the client's IPID or nil
--   server.getPlayerCount()   → int       current player count
--   server.log(msg)                       write to the server log at INFO level

-- ── Example: greet each player when they join ─────────────────────────────

function onClientJoin(uid)
    server.sendMessage(uid, "Welcome to the server! Type /help for a list of commands.")
    server.log("Client " .. tostring(uid) .. " joined (player count: " .. server.getPlayerCount() .. ")")
end

-- ── Example: log when a player leaves ────────────────────────────────────

function onClientLeave(uid)
    server.log("Client " .. tostring(uid) .. " left")
end

-- ── Example: add a custom /hello command ─────────────────────────────────
--
-- Returning `true` from onCommand suppresses the built-in C++ handler so
-- the client does not see "Unknown command." Return false (or nothing) to
-- let the normal handler run after the hook.

function onCommand(uid, cmd, args)
    if cmd == "hello" then
        local name = server.getClientName(uid) or "unknown"
        server.sendMessage(uid, "Hello, " .. name .. "! The server has " .. server.getPlayerCount() .. " player(s) online.")
        return true  -- handled; skip C++ handler
    end
    return false  -- not handled; let C++ process it
end
