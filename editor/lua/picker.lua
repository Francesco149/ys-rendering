-- picker.lua — Map / Mesh Gallery Picker UI with fuzzy search & 3D rotating hover thumbnails
local config = require("config")
local registry = require("map_registry")
local stage_loader = require("stage_loader")

local picker = {
    show_settings = false,
    selected_stage = nil,
    stage_to_open = nil,
    card_width = 240,
    card_height = 230,
    thumb_height = 140,
    temp_paths = {}, -- for settings modal
    texture_cache = {}, -- normalized_path -> tex_id
    current_page = 1,
    page_size = 24,
}

function picker.init()
    -- Check if first start
    if not config.settings.first_start_completed then
        picker.show_settings = true
    end

    -- Initial scan
    registry.rescan_all()
end

function picker.open_settings()
    picker.show_settings = true
    for gid, gconf in pairs(config.settings.games) do
        picker.temp_paths[gid] = gconf.archive_path or ""
    end
end
function picker.clear_texture_cache()
    if picker.texture_cache then
        for _, tex_id in pairs(picker.texture_cache) do
            if tex_id and tex_id > 1 then
                ys.dds.unload_texture(tex_id)
            end
        end
    end
    picker.texture_cache = {}
end

local function reset_unbound_texture_requests()
    if lp.async then lp.async.clear_pending() end
    for _, th in pairs(registry.thumbnails) do
        if th.requested_materials then
            for mat_idx, _ in pairs(th.requested_materials) do
                if not (th.bound_materials and th.bound_materials[mat_idx]) then
                    th.requested_materials[mat_idx] = nil
                end
            end
        end
    end
end


-- Process completed background tasks (non-blocking, called each frame)
function picker.process_async_completions()
    if not lp.async then return end
    local completed = lp.async.poll_completed(32)
    if not completed or #completed == 0 then return end

    for _, task in ipairs(completed) do
        if task.type == "load_texture" and task.success and task.tex_id then
            if task.path and task.path ~= "" then
                picker.texture_cache[task.path] = task.tex_id
            end
            local stage_key, mat_idx_str = task.tag:match("^(.-):(%d+)$")
            if stage_key and mat_idx_str then
                local thumb = registry.thumbnails[stage_key]
                if thumb and thumb.model_handle then
                    local mat_idx = tonumber(mat_idx_str)
                    ys.ymo.bind_texture(thumb.model_handle, mat_idx, task.tex_id)
                    thumb.bound_materials = thumb.bound_materials or {}
                    thumb.bound_materials[mat_idx] = true
                    thumb.has_textures = true
                    thumb.rendered = false -- trigger re-render with texture
                end
            end
        elseif task.type == "read_archive" and task.success and task.data then
            -- Submit CPU DDS decode task with same tag
            lp.async.decode_dds(task.data, false, task.tag)
        elseif task.type == "decode_dds" and task.success and task.tex_id then
            local stage_key, mat_idx_str = task.tag:match("^(.-):(%d+)$")
            if stage_key and mat_idx_str then
                local thumb = registry.thumbnails[stage_key]
                if thumb and thumb.model_handle then
                    local mat_idx = tonumber(mat_idx_str)
                    ys.ymo.bind_texture(thumb.model_handle, mat_idx, task.tex_id)
                    thumb.bound_materials = thumb.bound_materials or {}
                    thumb.bound_materials[mat_idx] = true
                    thumb.has_textures = true
                    thumb.rendered = false
                end
            end
        end
    end
end

-- Renders thumbnail into render texture if needed (budgeted per frame)
function picker.ensure_thumbnail_rendered(stage, dt)
    local thumb = registry.get_thumbnail(stage)

    -- 1. If not loaded yet, load minimal YMO or YCO for preview (respect per-frame budget)
    if not thumb.model_handle and not thumb.coll_handle and not thumb.is_loading then
        if (picker.models_loaded_this_frame or 0) >= 3 then
            -- Defer to next frame to keep smooth 60fps
            return
        end
        picker.models_loaded_this_frame = (picker.models_loaded_this_frame or 0) + 1
        thumb.is_loading = true
        local arch_handle = config.open_game_archive(stage.game_id)
        if arch_handle then
            if stage.ymo_path then
                local ymo_bytes = ys.archive.read_file(arch_handle, stage.ymo_path)
                if ymo_bytes then
                    local m_h, info = ys.ymo.load_from_memory(ymo_bytes, stage.stage_id .. ".ymo")
                    if m_h then
                        thumb.model_handle = m_h
                        thumb.info = info
                        thumb.model_path = stage.ymo_path
                        stage.total_triangles = info.total_triangles
                        stage.total_vertices = info.total_vertices
                    end
                end
            end

            -- If base model has very few triangles (< 50) and SOB exists, load primary prop from SOB
            if (not thumb.model_handle or (thumb.info and thumb.info.total_triangles < 50)) and stage.sob_path then
                local sob_bytes = ys.archive.read_file(arch_handle, stage.sob_path)
                if sob_bytes then
                    local sob_objs = ys.sob.parse_from_memory(sob_bytes)
                    if sob_objs and #sob_objs > 0 then
                        for _, obj in ipairs(sob_objs) do
                            if not obj.is_door_trigger and obj.model_path and obj.model_path ~= "" then
                                local norm_p = obj.model_path:gsub("^data[/\\]", ""):gsub("\\", "/"):lower()
                                local p_bytes = ys.archive.read_file(arch_handle, norm_p)
                                if p_bytes then
                                    local pm_h, p_info = ys.ymo.load_from_memory(p_bytes, norm_p)
                                    if pm_h and p_info and p_info.total_triangles > (stage.total_triangles or 0) then
                                        if thumb.model_handle then ys.ymo.unload(thumb.model_handle) end
                                        thumb.model_handle = pm_h
                                        thumb.info = p_info
                                        thumb.model_path = norm_p
                                        stage.total_triangles = p_info.total_triangles
                                        stage.total_vertices = p_info.total_vertices
                                        break
                                    end
                                end
                            end
                        end
                    end
                end
            end

            if not thumb.model_handle and stage.coll_s_path then
                local coll_bytes = ys.archive.read_file(arch_handle, stage.coll_s_path)
                if coll_bytes then
                    local c_h, info = ys.yco.load_from_memory(coll_bytes, "walkable", stage.stage_id .. "__s.yco")
                    if c_h then
                        thumb.coll_handle = c_h
                        stage.total_triangles = info.total_triangles
                    end
                end
            end
        end
        thumb.rendered = false
    end

    -- 2. Asynchronously stream textures in background (if enabled in settings)
    local allow_textures = (config.settings.thumbnail_textures ~= false)
    if thumb.model_handle and thumb.info and allow_textures then
        local gconf = config.settings.games[stage.game_id]
        if gconf and gconf.archive_path ~= "" then
            local q = config.settings.texture_quality or "H"
            local active_model_path = thumb.model_path or stage.ymo_path or ("map/" .. stage.stage_id .. "/" .. stage.stage_id .. ".ymo")
            local stage_dir = active_model_path:match("^(.-)/[^/]+$") or "map"
            local stage_parent = stage_dir:match("^(.-)/[^/]+$") or "map"

            thumb.bound_materials = thumb.bound_materials or {}
            thumb.requested_materials = thumb.requested_materials or {}

            for _, mat in ipairs(thumb.info.materials or {}) do
                local mat_idx = mat.index
                if not thumb.bound_materials[mat_idx] then
                    if not mat.texture_name or mat.texture_name == "" then
                        thumb.bound_materials[mat_idx] = true
                    else
                        local tex_name = mat.texture_name:lower()
                        local tex_base = tex_name:match("^([^%.]+)") or tex_name

                        local names_to_try = { tex_name, tex_base .. ".dds" }
                        if not tex_name:find("^_c_") then
                            table.insert(names_to_try, "_c_" .. tex_name)
                            table.insert(names_to_try, "_c_" .. tex_base .. ".dds")
                        else
                            local stripped = (tex_name:gsub("^_c_", ""))
                            table.insert(names_to_try, stripped)
                            local stripped_base = stripped:match("^([^%.]+)") or stripped
                            table.insert(names_to_try, stripped_base .. ".dds")
                        end

                        local search_dirs = {
                            stage_dir,
                            stage_dir .. "/" .. q:lower(),
                            stage_dir .. "/h",
                            stage_dir .. "/l",
                            stage_parent .. "/common/" .. q:lower(),
                            stage_parent .. "/common/h",
                            stage_parent .. "/common/l",
                            stage_parent .. "/common",
                            stage_parent,
                            "map/common/" .. q:lower(),
                            "map/common/h",
                            "map/common/l",
                            "map/common",
                            "map/mapobj/common/" .. q:lower(),
                            "map/mapobj/common/h",
                            "map/mapobj/common/l",
                            "map/mapobj/common",
                            "common/" .. q:lower(),
                            "common/h",
                            "common/l",
                            "common",
                        }

                        local candidates = {}
                        if mat.texture_path and mat.texture_path ~= "" then
                            local norm_tpath = (mat.texture_path:lower():gsub("\\", "/"))
                            table.insert(candidates, norm_tpath)
                        end
                        for _, sdir in ipairs(search_dirs) do
                            for _, ntry in ipairs(names_to_try) do
                                table.insert(candidates, sdir .. "/" .. ntry)
                            end
                        end

                        -- Check if any candidate is already in texture cache
                        local cached_tex_id = nil
                        for _, cand in ipairs(candidates) do
                            if picker.texture_cache[cand] then
                                cached_tex_id = picker.texture_cache[cand]
                                break
                            end
                        end

                        if cached_tex_id then
                            ys.ymo.bind_texture(thumb.model_handle, mat_idx, cached_tex_id)
                            thumb.bound_materials[mat_idx] = true
                            thumb.has_textures = true
                            thumb.rendered = false
                        elseif not thumb.requested_materials[mat_idx] and lp.async then
                            thumb.requested_materials[mat_idx] = true
                            local auto_lum = (tex_name:sub(1, 2) == "z_") or (mat.alpha < 0.95 and mat.alpha > 0.0)
                            local tag = stage.key .. ":" .. tostring(mat_idx)
                            lp.async.load_archive_texture(gconf.archive_path, candidates, auto_lum, tag)
                        end
                    end
                end
            end
        end
    end

    -- 3. Render 3D thumbnail to texture (settle GPU render texture over 2 initial frames)
    thumb.rendered_frames = thumb.rendered_frames or 0
    if not thumb.rendered or thumb.rendered_frames < 2 or thumb.is_hovered then
        local mh = thumb.model_handle or -1
        local ch = thumb.coll_handle or -1
        local untextured = not allow_textures or not thumb.has_textures
        lp.rl.render_map_thumbnail(thumb.rt_id, mh, ch, thumb.rot_y, untextured)
        thumb.rendered = true
        thumb.rendered_frames = thumb.rendered_frames + 1
    end
end
local ensure_thumbnail_rendered = picker.ensure_thumbnail_rendered

function picker.render_header(avail_w)
    ig.child("##header_bar", 0, 68, function()
        -- Row 1: Title, Texture Streaming Toggle, and Settings Button
        ig.text_colored("Ys Map & Mesh Viewer", 0.96, 0.62, 0.04, 1.0)
        ig.same_line()
        ig.text_colored(" |  Falcom Napishtim Engine Stage Explorer", 0.6, 0.65, 0.75, 1.0)

        local tex_enabled = (config.settings.thumbnail_textures ~= false)
        local tex_btn_w = 120
        local set_btn_w = 100
        ig.same_line(avail_w - tex_btn_w - set_btn_w - 20)
        if tex_enabled then
            ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        local tex_label = tex_enabled and "Textures: ON" or "Textures: OFF"
        if ig.button(tex_label .. "##thumb_tex_toggle", tex_btn_w, 24) then
            config.settings.thumbnail_textures = not tex_enabled
            reset_unbound_texture_requests()
            for _, th in pairs(registry.thumbnails) do
                th.rendered = false
            end
        end
        if tex_enabled then ig.pop_style_color(2) end

        ig.same_line(0, 8)
        if ig.button((ig.icon and (ig.icon.GEAR .. " ") or "") .. "Settings", set_btn_w, 24) then
            picker.open_settings()
        end

        ig.spacing()

        -- Row 2: Game Filter Tabs & Search Bar
        local tabs = {
            { id = "all", label = "All Games" },
            { id = "felghana", label = "Ys: Felghana" },
            { id = "origin", label = "Ys Origin" },
            { id = "ys6", label = "Ys VI" },
        }

        for i, tab in ipairs(tabs) do
            if i > 1 then ig.same_line(0, 4) end
            local is_sel = (registry.selected_game == tab.id)
            if is_sel then
                ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
                ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
            end

            local count = 0
            for _, s in ipairs(registry.stages) do
                if tab.id == "all" or s.game_id == tab.id then
                    count = count + 1
                end
            end

            local tab_label = string.format("%s (%d)", tab.label, count)
            if ig.button(tab_label .. "##tab_" .. tab.id, 0, 24) then
                registry.selected_game = tab.id
                registry.apply_filter()
                picker.current_page = 1
                reset_unbound_texture_requests()
            end

            if is_sel then
                ig.pop_style_color(2)
            end
        end

        local has_query = (registry.search_query and registry.search_query ~= "")
        local search_w = has_query and 225 or 255
        local total_search_w = has_query and (search_w + 30) or search_w
        ig.same_line(avail_w - total_search_w - 12)
        ig.push_item_width(search_w)
        local changed, new_query = ig.input_text_with_hint("##search_maps", "Search maps (or just type)...", registry.search_query)
        if changed then
            registry.search_query = new_query
            registry.apply_filter()
            picker.current_page = 1
            reset_unbound_texture_requests()
        end
        ig.pop_item_width()

        if has_query then
            ig.same_line(0, 4)
            if ig.button("X##clear_search", 24, 24) then
                registry.search_query = ""
                registry.apply_filter()
                picker.current_page = 1
                reset_unbound_texture_requests()
            end
        end
    end)
end

function picker.render_card(stage, dt)
    local thumb = registry.get_thumbnail(stage)
    ensure_thumbnail_rendered(stage, dt)

    local card_w = picker.card_width
    local card_h = picker.card_height

    ig.child("##card_" .. stage.key, card_w, card_h, function()
        local dl = ig.get_window_draw_list()
        local is_hovered = ig.is_window_hovered()
        registry.update_thumbnail_hover(stage, is_hovered, dt)

        local cur_x, cur_y = ig.get_cursor_screen_pos()
        local tw = card_w - 16
        local th = picker.thumb_height
        local is_ready = (thumb.model_handle ~= nil or thumb.coll_handle ~= nil)

        if not is_ready then
            -- Card is actively loading: distinct cyan/amber pulsing border + "Loading 3D Mesh..."
            local time_s = picker.anim_time or 0.0
            local pulse = 0.5 + 0.5 * math.sin(time_s * 5.0)
            ig.dl_add_rect_filled(dl, cur_x, cur_y, cur_x + tw, cur_y + th, 0.10, 0.11, 0.14, 1.0, 4.0)
            ig.dl_add_rect(dl, cur_x, cur_y, cur_x + tw, cur_y + th, 0.20 + 0.35 * pulse, 0.50 + 0.40 * pulse, 0.70 + 0.25 * pulse, 0.9, 4.0, 1.5)
            ig.dl_add_text(dl, cur_x + tw * 0.5 - 38, cur_y + th * 0.5 - 7, 0.40 + 0.45 * pulse, 0.75 + 0.20 * pulse, 0.95, 0.95, "Loading 3D Mesh...")
        else
            local gl_id = lp.rl.get_render_texture_gl_id(thumb.rt_id)
            if stage.stage_id == "S_0000" and not picker.s0000_logged then
                picker.s0000_logged = true
                print(string.format("[picker DEBUG] S_0000: rt_id=%d, gl_id=%d, model_h=%s, total_tris=%d",
                    thumb.rt_id, gl_id, tostring(thumb.model_handle), stage.total_triangles or 0))
            end
            ig.dl_add_rect_filled(dl, cur_x, cur_y, cur_x + tw, cur_y + th, 0.08, 0.09, 0.11, 1.0, 4.0)
            ig.dl_add_image(dl, gl_id, cur_x, cur_y, cur_x + tw, cur_y + th, 0, 1, 1, 0, 1, 1, 1, 1)

            if is_hovered then
                ig.dl_add_rect(dl, cur_x - 1, cur_y - 1, cur_x + tw + 1, cur_y + th + 1, 0.96, 0.62, 0.04, 1.0, 4.0, 2.0)
                ig.dl_add_text(dl, cur_x + 6, cur_y + th - 18, 0.96, 0.62, 0.04, 0.9, "Rotating...")
            else
                ig.dl_add_rect(dl, cur_x, cur_y, cur_x + tw, cur_y + th, 0.2, 0.22, 0.28, 1.0, 4.0, 1.0)
            end
        end

        if ig.invisible_button("##btn_" .. stage.key, tw, th) then
            picker.stage_to_open = stage
        end

        ig.spacing()

        local bc = stage.badge_color or { 0.96, 0.62, 0.04, 1.0 }
        ig.text_colored("[" .. stage.game_name .. "]", bc[1], bc[2], bc[3], bc[4])
        ig.same_line()
        ig.text_colored(stage.stage_id, 0.9, 0.9, 0.95, 1.0)

        local title = stage.title or stage.stage_id
        if #title > 28 then title = title:sub(1, 26) .. ".." end
        ig.text(title)

        local stats_str = stage.zone or "Map"
        if stage.total_triangles and stage.total_triangles > 0 then
            stats_str = stats_str .. " · " .. tostring(stage.total_triangles) .. " tris"
        end
        ig.text_colored(stats_str, 0.5, 0.55, 0.65, 1.0)
    end)
end

function picker.render_grid(avail_w, avail_h, dt)
    ig.child("##stage_gallery", 0, 0, function()
        local stages = registry.filtered
        if #stages == 0 then
            ig.spacing()
            ig.text_colored("No maps found matching criteria.", 0.7, 0.7, 0.75, 1.0)
            ig.spacing()
            if ig.button("Open Settings to Configure Game Paths", 260, 30) then
                picker.open_settings()
            end
            return
        end

        local card_w = picker.card_width
        local card_spacing = 14
        local num_cols = math.max(1, math.floor((avail_w - 20) / (card_w + card_spacing)))

        local total_pages = math.max(1, math.ceil(#stages / picker.page_size))
        picker.current_page = math.max(1, math.min(picker.current_page, total_pages))

        local start_idx = (picker.current_page - 1) * picker.page_size + 1
        local end_idx = math.min(#stages, start_idx + picker.page_size - 1)

        -- Render cards for current page
        for i = start_idx, end_idx do
            local stage = stages[i]
            local card_num = i - start_idx + 1
            local col = (card_num - 1) % num_cols
            if col > 0 then
                ig.same_line(0, card_spacing)
            end
            picker.render_card(stage, dt)
        end

        ig.spacing()
        ig.separator()
        ig.spacing()

        -- Pagination Bar
        if total_pages > 1 then
            local function on_page_changed()
                reset_unbound_texture_requests()
            end

            if ig.button("<< First", 70, 24) and picker.current_page > 1 then
                picker.current_page = 1
                on_page_changed()
            end
            ig.same_line(0, 4)
            if ig.button("< Prev", 60, 24) and picker.current_page > 1 then
                picker.current_page = picker.current_page - 1
                on_page_changed()
            end
            ig.same_line(0, 8)

            local page_str = string.format("Page %d of %d  (%d maps)", picker.current_page, total_pages, #stages)
            ig.text_colored(page_str, 0.9, 0.9, 0.95, 1.0)
            ig.same_line(0, 8)

            if ig.button("Next >", 60, 24) and picker.current_page < total_pages then
                picker.current_page = picker.current_page + 1
                on_page_changed()
            end
            ig.same_line(0, 4)
            if ig.button("Last >>", 70, 24) and picker.current_page < total_pages then
                picker.current_page = total_pages
                on_page_changed()
            end
        end
    end)
end

function picker.render_settings_modal()
    if not picker.show_settings then return end

    local flags = (ig.wflag and (ig.wflag.AlwaysAutoResize | ig.wflag.NoCollapse)) or 0
    ig.set_next_window_size(620, 0, ig.Cond_Always)

    ig.popup_modal("Game Archive Settings & Folder Discovery", flags, function()
        ig.text_colored("Configure Game Paths for Direct Map & Archive Extraction", 0.96, 0.62, 0.04, 1.0)
        ig.separator()
        ig.spacing()

        ig.text_wrapped("The viewer extracts maps, 3D meshes, textures, and collisions directly from the game's data.ni / data.na archives without needing pre-extraction.")
        ig.spacing()

        -- Quality Selector
        ig.text("Texture Quality:")
        ig.same_line()
        if ig.radio_button("High (256x256)", config.settings.texture_quality == "H") then
            config.settings.texture_quality = "H"
        end
        ig.same_line()
        if ig.radio_button("Low (128x128)", config.settings.texture_quality == "L") then
            config.settings.texture_quality = "L"
        end

        ig.spacing()
        ig.separator()
        ig.spacing()

        -- Game Entries
        local g_order = { "felghana", "origin", "ys6" }
        for _, gid in ipairs(g_order) do
            local gdef = config.game_defs[gid]
            local gconf = config.settings.games[gid]

            ig.push_id(gid)

            -- Enable Checkbox
            local en_changed, new_en = ig.checkbox("##en", gconf.enabled)
            if en_changed then gconf.enabled = new_en end
            ig.same_line()

            -- Game Name with Badge Color
            local bc = gdef.badge_color
            ig.text_colored(gdef.name, bc[1], bc[2], bc[3], bc[4])

            -- Auto-detected indicator
            if gconf.detected_path and gconf.detected_path ~= "" then
                ig.same_line()
                ig.text_colored(" (Steam Detected)", 0.2, 0.85, 0.4, 1.0)
            end

            -- Path Input & Browse Button
            ig.indent(24)
            ig.push_item_width(450)
            local current_path = picker.temp_paths[gid] or gconf.archive_path or ""
            local p_changed, new_p = ig.input_text_with_hint("##path", "Path to data.ni or release/data.ni...", current_path)
            if p_changed then
                picker.temp_paths[gid] = new_p
                gconf.archive_path = new_p
            end
            ig.pop_item_width()

            ig.same_line()
            if ig.button("Browse...##b_" .. gid, 80, 22) then
                local res = lp.app.open_file_dialog("Select Falcom data.ni archive", current_path, "*.ni")
                if res and res ~= "" then
                    picker.temp_paths[gid] = res
                    gconf.archive_path = res
                    gconf.enabled = true
                end
            end
            ig.unindent(24)

            ig.spacing()
            ig.pop_id()
        end

        ig.separator()
        ig.spacing()

        -- Bottom Action Buttons
        if ig.button("Auto-Detect Steam Games", 180, 28) then
            config.detect_all_games()
            for gid, gconf in pairs(config.settings.games) do
                picker.temp_paths[gid] = gconf.archive_path
            end
        end

        ig.same_line()
        if ig.button("Save & Rescan Maps", 160, 28) then
            for gid, gconf in pairs(config.settings.games) do
                gconf.archive_path = picker.temp_paths[gid] or gconf.archive_path
            end
            config.settings.first_start_completed = true
            config.save_settings()
            config.close_all_archives()
            stage_loader.clear_texture_cache()
            picker.clear_texture_cache()
            registry.cleanup_thumbnails()
            registry.rescan_all()
            picker.show_settings = false
            ig.close_current_popup()
        end

        ig.same_line()
        if ig.button("Cancel", 90, 28) then
            picker.show_settings = false
            ig.close_current_popup()
        end
    end)
end

function picker.frame(dt)
    picker.anim_time = (picker.anim_time or 0.0) + dt
    picker.models_loaded_this_frame = 0

    -- Global Keyboard Input (Search typing anywhere & ESC navigation)
    local io = ig.get_io()
    if not picker.show_settings and not io.want_capture_keyboard then
        -- 1. ESC hotkey: clear search filter
        local esc_pressed = lp.rl.is_key_pressed(lp.rl.key.Escape) or (ig.key and ig.is_key_pressed(ig.key.Escape))
        if esc_pressed and registry.search_query ~= "" then
            registry.search_query = ""
            registry.apply_filter()
            picker.current_page = 1
            reset_unbound_texture_requests()
        end

        -- 2. Backspace: remove last character
        if lp.rl.is_key_pressed(lp.rl.key.Backspace) or (ig.key and ig.is_key_pressed(ig.key.Backspace)) then
            if #registry.search_query > 0 then
                registry.search_query = registry.search_query:sub(1, -2)
                registry.apply_filter()
                picker.current_page = 1
                reset_unbound_texture_requests()
            end
        end

        -- 3. Printable characters: type anywhere to search
        if lp.rl.get_char_pressed then
            local ch = lp.rl.get_char_pressed()
            local text_changed = false
            while ch > 0 do
                if ch >= 32 and ch <= 126 then
                    registry.search_query = registry.search_query .. string.char(ch)
                    text_changed = true
                end
                ch = lp.rl.get_char_pressed()
            end
            if text_changed then
                registry.apply_filter()
                picker.current_page = 1
                reset_unbound_texture_requests()
            end
        end
    elseif picker.show_settings and not io.want_capture_keyboard then
        -- ESC closes settings modal
        local esc_pressed = lp.rl.is_key_pressed(lp.rl.key.Escape) or (ig.key and ig.is_key_pressed(ig.key.Escape))
        if esc_pressed then
            picker.show_settings = false
        end
    end
    picker.process_async_completions()
    local screen_w, screen_h = lp.rl.get_screen_size()
    local flags = (ig.wflag and (ig.wflag.NoDecoration | ig.wflag.NoMove | ig.wflag.NoBringToFrontOnFocus)) or 0

    ig.set_next_window_pos(0, 0, ig.Cond_Always)
    ig.set_next_window_size(screen_w, screen_h, ig.Cond_Always)

    ig.window("##map_picker_main", flags, function()
        local avail_w, avail_h = ig.get_content_region_avail()
        picker.render_header(avail_w)
        picker.render_grid(avail_w, avail_h - 76, dt)
    end)

    if picker.show_settings then
        ig.open_popup("Game Archive Settings & Folder Discovery")
        picker.render_settings_modal()
    end

    -- Return selected stage to open if clicked
    local to_open = picker.stage_to_open
    picker.stage_to_open = nil
    return to_open
end

return picker
