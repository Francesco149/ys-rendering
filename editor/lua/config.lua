-- config.lua — Game path discovery, settings persistence, and archive management
local config = {
    settings = {
        first_start_completed = false,
        texture_quality = "H", -- "H" (High 256x256) or "L" (Low 128x128)
        games = {
            felghana = {
                enabled = true,
                archive_path = "",
                detected_path = "",
            },
            origin = {
                enabled = true,
                archive_path = "",
                detected_path = "",
            },
            ys6 = {
                enabled = true,
                archive_path = "",
                detected_path = "",
            },
        }
    },
    game_defs = {
        felghana = {
            id = "felghana",
            name = "Ys: The Oath in Felghana",
            short_name = "Felghana",
            badge_color = { 0.96, 0.45, 0.15, 1.0 }, -- Orange
            default_steam_subdirs = {
                "Ys The Oath in Felghana/release",
                "Ys The Oath in Felghana",
            },
        },
        origin = {
            id = "origin",
            name = "Ys Origin",
            short_name = "Origin",
            badge_color = { 0.20, 0.70, 0.95, 1.0 }, -- Cyan/Blue
            default_steam_subdirs = {
                "Ys Origin/release",
                "Ys Origin",
            },
        },
        ys6 = {
            id = "ys6",
            name = "Ys VI: The Ark of Napishtim",
            short_name = "Ys VI",
            badge_color = { 0.85, 0.25, 0.40, 1.0 }, -- Crimson/Pink
            default_steam_subdirs = {
                "Ys VI/release",
                "Ys VI",
            },
        },
    },
    active_archives = {}, -- game_id -> archive handle
}

local function file_exists(path)
    if not path or path == "" then return false end
    return lp.app.file_exists(path)
end

-- Common Steam search root directories
local STEAM_ROOTS = {
    -- Windows / WSL mount paths
    "/mnt/c/Program Files (x86)/Steam/steamapps/common",
    "/mnt/c/Program Files/Steam/steamapps/common",
    "/mnt/d/Steam/steamapps/common",
    "/mnt/d/SteamLibrary/steamapps/common",
    "/mnt/e/SteamLibrary/steamapps/common",
    "C:/Program Files (x86)/Steam/steamapps/common",
    "C:/Program Files/Steam/steamapps/common",
    "C:/Steam/steamapps/common",
    "C:/SteamLibrary/steamapps/common",
    "D:/SteamLibrary/steamapps/common",
    "E:/SteamLibrary/steamapps/common",
    -- Linux Native Steam paths
    "~/.local/share/Steam/steamapps/common",
    "~/.steam/steam/steamapps/common",
    "~/.steam/root/steamapps/common",
    -- Local repository / extracted fallbacks
    "./extracted",
    "../extracted",
    "./data",
}

function config.auto_detect_game_path(game_id)
    local def = config.game_defs[game_id]
    if not def then return nil end

    for _, root in ipairs(STEAM_ROOTS) do
        -- Expand ~ to HOME if on Linux
        local expanded_root = root
        if root:sub(1, 2) == "~/" then
            local home = os.getenv("HOME") or ""
            expanded_root = home .. root:sub(2)
        end

        for _, subdir in ipairs(def.default_steam_subdirs) do
            local base = expanded_root .. "/" .. subdir
            local candidates = {
                base .. "/data.ni",
                base .. "/DATA.NI",
                base .. "/release/data.ni",
                base .. "/release/DATA.NI",
            }
            for _, cand in ipairs(candidates) do
                if file_exists(cand) then
                    return cand
                end
            end
        end
    end
    return nil
end

function config.detect_all_games()
    for id, _ in pairs(config.game_defs) do
        local detected = config.auto_detect_game_path(id)
        if detected then
            config.settings.games[id].detected_path = detected
            if config.settings.games[id].archive_path == "" then
                config.settings.games[id].archive_path = detected
            end
            config.settings.games[id].enabled = true
        else
            config.settings.games[id].detected_path = ""
            if config.settings.games[id].archive_path == "" then
                config.settings.games[id].enabled = false
            end
        end
    end
end

function config.load_settings()
    local content = lp.app.load_user_file("settings.json")
    if content and #content > 0 then
        -- Simple JSON key-value extraction for robustness without external deps
        for gid, _ in pairs(config.game_defs) do
            local en_pat = '"' .. gid .. '"%s*:%s*{[^}]*"enabled"%s*:%s*(%a+)'
            local en_val = content:match(en_pat)
            if en_val ~= nil then
                config.settings.games[gid].enabled = (en_val == "true")
            end

            local path_pat = '"' .. gid .. '"%s*:%s*{[^}]*"archive_path"%s*:%s*"([^"]*)"'
            local path_val = content:match(path_pat)
            if path_val and path_val ~= "" then
                config.settings.games[gid].archive_path = path_val
            end
        end

        local fs_val = content:match('"first_start_completed"%s*:%s*(%a+)'):match("true")
        if fs_val then config.settings.first_start_completed = true end

        local tex_val = content:match('"texture_quality"%s*:%s*"([HL])"')
        if tex_val then config.settings.texture_quality = tex_val end
    end

    -- Run detection for any unconfigured paths
    config.detect_all_games()
end

function config.save_settings()
    local json = "{\n"
    json = json .. '  "first_start_completed": ' .. tostring(config.settings.first_start_completed) .. ',\n'
    json = json .. '  "texture_quality": "' .. config.settings.texture_quality .. '",\n'
    json = json .. '  "games": {\n'
    local g_keys = { "felghana", "origin", "ys6" }
    for i, gid in ipairs(g_keys) do
        local g = config.settings.games[gid]
        local comma = (i < #g_keys) and "," or ""
        json = json .. '    "' .. gid .. '": {\n'
        json = json .. '      "enabled": ' .. tostring(g.enabled) .. ',\n'
        json = json .. '      "archive_path": "' .. (g.archive_path or "") .. '"\n'
        json = json .. '    }' .. comma .. '\n'
    end
    json = json .. '  }\n'
    json = json .. '}\n'

    lp.app.save_user_file("settings.json", json)
end

function config.open_game_archive(game_id)
    if config.active_archives[game_id] then
        return config.active_archives[game_id]
    end

    local g_conf = config.settings.games[game_id]
    if not g_conf or not g_conf.enabled or g_conf.archive_path == "" then
        return nil, "Game disabled or no archive path"
    end

    local handle, err = ys.archive.open(g_conf.archive_path)
    if not handle then
        return nil, err or "Failed to open archive: " .. g_conf.archive_path
    end

    config.active_archives[game_id] = handle
    return handle
end

function config.close_game_archive(game_id)
    if config.active_archives[game_id] then
        ys.archive.close(config.active_archives[game_id])
        config.active_archives[game_id] = nil
    end
end

function config.close_all_archives()
    for gid, handle in pairs(config.active_archives) do
        ys.archive.close(handle)
    end
    config.active_archives = {}
end

return config
