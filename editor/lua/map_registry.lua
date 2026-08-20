-- map_registry.lua — Known stage metadata, archive scanning, and stage definitions
local config = require("config")

local registry = {
    stages = {},        -- list of all discovered stage descriptors
    filtered = {},      -- currently filtered stages for UI
    selected_game = "all", -- "all", "felghana", "origin", "ys6"
    search_query = "",
    thumbnails = {},    -- stage_key -> { rt_id, rot_y, rendered, is_loading }
}

local KNOWN_STAGE_TITLES = {
    felghana = {
        S_0000 = { title = "Opening Ship / Port Arrival", zone = "Port" },
        S_0100 = { title = "Town of Redmont - Tavern & Inn", zone = "Redmont" },
        S_0110 = { title = "Town of Redmont - Weapon Shop", zone = "Redmont" },
        S_0120 = { title = "Town of Redmont - Mayor's House", zone = "Redmont" },
        S_0130 = { title = "Town of Redmont - Residential Quarter", zone = "Redmont" },
        S_0140 = { title = "Town of Redmont - Private House", zone = "Redmont" },
        S_0150 = { title = "Town of Redmont - Chapel", zone = "Redmont" },
        S_0200 = { title = "Town Outskirts & Gate", zone = "Redmont Outskirts" },
        S_1000 = { title = "Tigray Quarry - Exterior Canyon", zone = "Tigray Quarry" },
        S_1010 = { title = "Tigray Quarry - Mine Tunnels", zone = "Tigray Quarry" },
        S_1020 = { title = "Tigray Quarry - Deep Shaft", zone = "Tigray Quarry" },
        S_1030 = { title = "Tigray Quarry - Storehouse", zone = "Tigray Quarry" },
        S_1040 = { title = "Tigray Quarry - Underground Basin", zone = "Tigray Quarry" },
        S_2000 = { title = "Ruins of Illburns - Temple Entrance", zone = "Ruins of Illburns" },
        S_2010 = { title = "Ruins of Illburns - Lava Basin", zone = "Ruins of Illburns" },
        S_2020 = { title = "Ruins of Illburns - Sacrificial Altar", zone = "Ruins of Illburns" },
        S_2500 = { title = "Lava Zone - Magma Caverns", zone = "Lava Zone" },
        S_2510 = { title = "Lava Zone - Fire Sanctuary", zone = "Lava Zone" },
        S_3000 = { title = "Elderm Mountains - Snow Base", zone = "Elderm Mountains" },
        S_3010 = { title = "Elderm Mountains - High Peaks", zone = "Elderm Mountains" },
        S_3100 = { title = "Elderm Mountains - Ice Caverns", zone = "Elderm Mountains" },
        S_3110 = { title = "Elderm Mountains - Glacial Abyss", zone = "Elderm Mountains" },
        S_3500 = { title = "Valestein Castle - Courtyard", zone = "Valestein Castle" },
        S_3510 = { title = "Valestein Castle - Corridors & Chambers", zone = "Valestein Castle" },
        S_3520 = { title = "Valestein Castle - Clock Tower", zone = "Valestein Castle" },
        S_3530 = { title = "Valestein Castle - Throne Room", zone = "Valestein Castle" },
        S_5000 = { title = "Genos Island - Sanctuary Approach", zone = "Genos Island" },
        S_5010 = { title = "Genos Island - Dark Palace", zone = "Genos Island" },
        S_5020 = { title = "Genos Island - Core of Rebirth", zone = "Genos Island" },
    },
    origin = {
        S_0000 = { title = "Darm Tower - Ground Floor", zone = "Darm Tower" },
        S_0100 = { title = "Floor of Wailing - Entrance", zone = "Floor of Wailing" },
        S_0110 = { title = "Floor of Wailing - Outer Halls", zone = "Floor of Wailing" },
        S_0200 = { title = "Floor of Flooding - Waterway", zone = "Floor of Flooding" },
        S_0210 = { title = "Floor of Flooding - Aqueduct", zone = "Floor of Flooding" },
        S_0300 = { title = "Guillotine Area - Lower Prison", zone = "Guillotine Area" },
        S_0310 = { title = "Guillotine Area - Execution Grounds", zone = "Guillotine Area" },
        S_0400 = { title = "Silent Sands - Desert Dunes", zone = "Silent Sands" },
        S_0410 = { title = "Silent Sands - Quicksand Cavern", zone = "Silent Sands" },
        S_0500 = { title = "Mirror Corridor - Hall of Reflection", zone = "Mirror Corridor" },
        S_0600 = { title = "Demonic Core - Dark Chamber", zone = "Demonic Core" },
        S_1000 = { title = "Tower Summit - Rado's Annex", zone = "Tower Summit" },
        S_2000 = { title = "Devil's Throne - Peak of Origin", zone = "Devil's Throne" },
    },
    ys6 = {
        S_0000 = { title = "Port Rimorge - Harbor", zone = "Canaan Island" },
        S_0100 = { title = "Port Rimorge - Town Center", zone = "Canaan Island" },
        S_0110 = { title = "Port Rimorge - Tavern & Forge", zone = "Canaan Island" },
        S_0200 = { title = "Redmont Shore - Shipwreck", zone = "Canaan Island" },
        S_1000 = { title = "Canaan Plains - Grassland", zone = "Canaan Plains" },
        S_1010 = { title = "Canaan Plains - Ravine", zone = "Canaan Plains" },
        S_1100 = { title = "Quatera Woods - Ancient Forest", zone = "Quatera Woods" },
        S_1110 = { title = "Quatera Woods - Spring of Spirits", zone = "Quatera Woods" },
        S_2000 = { title = "Zemeth Sanctuary - Temple Gate", zone = "Zemeth Sanctuary" },
        S_2010 = { title = "Zemeth Sanctuary - Inner Altar", zone = "Zemeth Sanctuary" },
        S_3000 = { title = "Limbless Ruin - Stone Bridge", zone = "Limbless Ruin" },
        S_4000 = { title = "Cradle of Time - Clockwork Core", zone = "Cradle of Time" },
    },
}

function registry.scan_game_stages(game_id)
    local handle = config.open_game_archive(game_id)
    if not handle then return {} end

    local files = ys.archive.list_files(handle, "map/")
    local stages_map = {}

    for _, file in ipairs(files) do
        local path = file.path:lower()
        local filename = file.path:match("([^/]+)$") or file.path
        local fname_lower = filename:lower()

        -- Check for Ys Origin collision mesh companion (e.g. s_1000_.ymo)
        local under_base = fname_lower:match("^(s_[%w]+)_+%.ymo$")
        if under_base then
            local base_id = under_base:upper()
            if not stages_map[base_id] then
                stages_map[base_id] = {
                    game_id = game_id,
                    stage_id = base_id,
                    sob_path = nil,
                    ymo_path = nil,
                    coll_mesh_path = nil,
                    coll_s_path = nil,
                    coll_w_path = nil,
                    coll_c_path = nil,
                    scm_path = nil,
                }
            end
            stages_map[base_id].coll_mesh_path = file.path
        else
            -- Look for patterns like map/s_01/s_0100/s_0100.sob or map/s_0100/s_0100.sob
            local stage_id = path:match("(s_[%w]+)%.sob") or path:match("(s_[%w]+)%.ymo")
            if stage_id then
                stage_id = stage_id:upper()
                -- Ignore mapobj submodel definitions
                if not stage_id:find("MAPOBJ") and not stage_id:find("DOR") then
                    if not stages_map[stage_id] then
                        stages_map[stage_id] = {
                            game_id = game_id,
                            stage_id = stage_id,
                            sob_path = nil,
                            ymo_path = nil,
                            coll_mesh_path = nil,
                            coll_s_path = nil,
                            coll_w_path = nil,
                            coll_c_path = nil,
                            scm_path = nil,
                        }
                    end

                    local st = stages_map[stage_id]
                    local exact_ymo = (stage_id .. ".ymo"):lower()
                    local exact_sob = (stage_id .. ".sob"):lower()
                    local exact_under = (stage_id .. "_.ymo"):lower()

                    if fname_lower == exact_sob then
                        st.sob_path = file.path
                    elseif fname_lower == exact_ymo then
                        st.ymo_path = file.path
                    elseif fname_lower == exact_under then
                        st.coll_mesh_path = file.path
                    elseif not st.ymo_path and fname_lower:find("%.ymo$") and not fname_lower:find("__") and not fname_lower:find("_%.ymo$") then
                        st.ymo_path = file.path
                    elseif fname_lower:find("__s%.yco$") or fname_lower:find("_s%.yco$") then
                        st.coll_s_path = file.path
                    elseif fname_lower:find("__w%.yco$") or fname_lower:find("_w%.yco$") then
                        st.coll_w_path = file.path
                    elseif fname_lower:find("__c%.yco$") or fname_lower:find("_c%.yco$") then
                        st.coll_c_path = file.path
                    elseif fname_lower:find("%.scm$") then
                        st.scm_path = file.path
                    end
                end
            end
        end
    end
    -- Associate titles and build list
    local result = {}
    local known = KNOWN_STAGE_TITLES[game_id] or {}

    for sid, st in pairs(stages_map) do
        local info = known[sid]
        if info then
            st.title = info.title
            st.zone = info.zone
        else
            st.title = sid .. " (Stage)"
            st.zone = "Map " .. sid:sub(1, 4)
        end
        st.game_name = config.game_defs[game_id].short_name
        st.badge_color = config.game_defs[game_id].badge_color
        st.key = game_id .. ":" .. sid

        -- If YMO wasn't explicitly found, derive expected path from SOB path
        if not st.ymo_path and st.sob_path then
            st.ymo_path = st.sob_path:gsub("%.[Ss][Oo][Bb]$", ".YMO")
        end
        if not st.coll_mesh_path and st.ymo_path then
            st.coll_mesh_path = st.ymo_path:gsub("%.[Yy][Mm][Oo]$", "_.YMO")
        end
        if not st.coll_s_path and st.ymo_path then
            st.coll_s_path = st.ymo_path:gsub("%.[Yy][Mm][Oo]$", "__s.YCO")
        end
        if not st.coll_w_path and st.ymo_path then
            st.coll_w_path = st.ymo_path:gsub("%.[Yy][Mm][Oo]$", "__w.YCO")
        end
        if not st.coll_c_path and st.ymo_path then
            st.coll_c_path = st.ymo_path:gsub("%.[Yy][Mm][Oo]$", "__c.YCO")
        end
        if not st.scm_path and st.ymo_path then
            st.scm_path = st.ymo_path:gsub("%.[Yy][Mm][Oo]$", ".SCM")
        end

        table.insert(result, st)
    end

    table.sort(result, function(a, b)
        return a.stage_id < b.stage_id
    end)

    return result
end

function registry.rescan_all()
    registry.stages = {}
    for gid, gconf in pairs(config.settings.games) do
        if gconf.enabled and gconf.archive_path ~= "" then
            local game_stages = registry.scan_game_stages(gid)
            for _, st in ipairs(game_stages) do
                table.insert(registry.stages, st)
            end
        end
    end
    registry.apply_filter()
end

function registry.apply_filter()
    local filtered = {}
    local query = registry.search_query:lower():gsub("%s+", "")

    for _, st in ipairs(registry.stages) do
        local match_game = (registry.selected_game == "all" or st.game_id == registry.selected_game)
        if match_game then
            if query == "" then
                table.insert(filtered, st)
            else
                local combined = (st.stage_id .. " " .. st.title .. " " .. st.zone .. " " .. st.game_name):lower():gsub("%s+", "")
                if combined:find(query, 1, true) then
                    table.insert(filtered, st)
                end
            end
        end
    end

    registry.filtered = filtered
end

local MAX_THUMBNAILS = 256
local thumbnail_keys = {} -- array of keys in LRU order (index 1 is oldest, index #keys is newest)

local function touch_key(key)
    for i = 1, #thumbnail_keys do
        if thumbnail_keys[i] == key then
            table.remove(thumbnail_keys, i)
            break
        end
    end
    table.insert(thumbnail_keys, key)
end

function registry.get_thumbnail(stage)
    local key = stage.key
    if registry.thumbnails[key] then
        touch_key(key)
        return registry.thumbnails[key]
    end

    -- If reached capacity, evict oldest
    while #thumbnail_keys >= MAX_THUMBNAILS do
        local old_key = table.remove(thumbnail_keys, 1)
        local old_thumb = registry.thumbnails[old_key]
        if old_thumb then
            if old_thumb.model_handle then
                ys.ymo.unload(old_thumb.model_handle)
            end
            if old_thumb.coll_handle then
                ys.yco.unload(old_thumb.coll_handle)
            end
            lp.rl.unload_render_texture(old_thumb.rt_id)
            registry.thumbnails[old_key] = nil
        end
    end

    local rt_id = lp.rl.create_render_texture(256, 256)
    local thumb = {
        rt_id = rt_id,
        gl_id = lp.rl.get_render_texture_gl_id(rt_id),
        rot_y = 0.0,
        rendered = false,
        rendered_frames = 0,
        model_handle = nil,
        coll_handle = nil,
        is_hovered = false,
        is_loading = false,
        has_textures = false,
        bound_materials = {},
        requested_materials = {},
    }
    registry.thumbnails[key] = thumb
    table.insert(thumbnail_keys, key)
    return thumb
end

function registry.update_thumbnail_hover(stage, is_hovered, dt)
    local thumb = registry.thumbnails[stage.key]
    if not thumb then return end
    thumb.is_hovered = is_hovered
    if is_hovered then
        thumb.rot_y = (thumb.rot_y + dt * 45.0) % 360.0
        thumb.rendered = false
    end
end

function registry.cleanup_thumbnails()
    for _, t in pairs(registry.thumbnails) do
        if t.model_handle then
            ys.ymo.unload(t.model_handle)
        end
        if t.coll_handle then
            ys.yco.unload(t.coll_handle)
        end
        lp.rl.unload_render_texture(t.rt_id)
    end
    registry.thumbnails = {}
    thumbnail_keys = {}
end

return registry
