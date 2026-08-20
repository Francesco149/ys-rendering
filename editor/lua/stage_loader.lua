-- stage_loader.lua — Composite stage scene loader, texture binder, and scene renderer
local config = require("config")

local stage_loader = {
    current_scene = nil,
    texture_cache = {}, -- normalized_path -> tex_id
}

local function normalize_path(path)
    if not path then return "" end
    local norm = path:gsub("\\", "/"):lower()
    while norm:sub(1, 2) == "./" do norm = norm:sub(3) end
    while norm:sub(1, 1) == "/" do norm = norm:sub(2) end
    while norm:sub(1, 5) == "data/" do norm = norm:sub(6) end
    return norm
end

local function get_filename(path)
    local norm = normalize_path(path)
    return norm:match("([^/]+)$") or norm
end

local function get_parent_dir(path)
    local norm = normalize_path(path)
    return norm:match("^(.*)/[^/]+$") or ""
end

function stage_loader.load_texture_from_archive(arch_handle, candidate_paths, auto_lum)
    for _, path in ipairs(candidate_paths) do
        local norm = normalize_path(path)
        if stage_loader.texture_cache[norm] then
            return stage_loader.texture_cache[norm]
        end

        local bytes = ys.archive.read_file(arch_handle, norm)
        if bytes and #bytes > 128 then
            local is_lum = auto_lum or norm:find("z_") ~= nil or norm:find("hikari") ~= nil
            local tex_id, w, h = ys.dds.load_texture(bytes, is_lum)
            if tex_id and tex_id > 0 then
                stage_loader.texture_cache[norm] = tex_id
                return tex_id
            end
        end
    end
    return nil
end

function stage_loader.resolve_and_bind_textures(arch_handle, model_handle, info, stage_dir)
    if not info or not info.materials then return end

    local q = config.settings.texture_quality or "H"
    local stage_parent = get_parent_dir(stage_dir) -- e.g. "map/s_01"

    for _, mat in ipairs(info.materials) do
        if mat.texture_name and mat.texture_name ~= "" then
            local tex_name = mat.texture_name:lower()
            local tex_base = tex_name:match("^([^%.]+)") or tex_name

            local names_to_try = { tex_name, tex_base .. ".dds" }
            if not tex_name:find("^_c_") then
                table.insert(names_to_try, "_c_" .. tex_name)
                table.insert(names_to_try, "_c_" .. tex_base .. ".dds")
            else
                local stripped = tex_name:gsub("^_c_", "")
                table.insert(names_to_try, stripped)
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
                "map/common/" .. q:lower(),
                "map/common/h",
                "map/common/l",
                "map/common",
                "common/" .. q:lower(),
                "common/h",
                "common/l",
                "common",
            }

            local candidates = {}
            if mat.texture_path and mat.texture_path ~= "" then
                table.insert(candidates, mat.texture_path)
            end
            for _, sdir in ipairs(search_dirs) do
                for _, ntry in ipairs(names_to_try) do
                    table.insert(candidates, sdir .. "/" .. ntry)
                end
            end
            local auto_lum = (tex_name:sub(1, 2) == "z_") or (mat.alpha < 0.95 and mat.alpha > 0.0)
            local tex_id = stage_loader.load_texture_from_archive(arch_handle, candidates, auto_lum)
            if tex_id then
                ys.ymo.bind_texture(model_handle, mat.index, tex_id)
            end
        end
    end
end

function stage_loader.load_stage(stage_desc)
    stage_loader.unload_current_stage()

    local arch_handle = config.open_game_archive(stage_desc.game_id)
    if not arch_handle then
        return nil, "Could not open game archive for " .. stage_desc.game_name
    end

    local scene = {
        stage_desc = stage_desc,
        base_model_handle = nil,
        base_info = nil,
        placed_props = {},
        door_triggers = {},
        coll_walkable = nil,
        coll_wall = nil,
        coll_camera = nil,
        coll_origin_mesh = nil,
        scm_camera = nil,
        bounds = { min = { x = -20, y = 0, z = -20 }, max = { x = 20, y = 10, z = 20 } },
        center = { x = 0, y = 0, z = 0 },
        radius = 30.0,
        total_triangles = 0,
        total_vertices = 0,
    }

    local stage_dir = get_parent_dir(stage_desc.sob_path or stage_desc.ymo_path or "map/" .. stage_desc.stage_id)

    -- 1. Load Base Map YMO
    if stage_desc.ymo_path then
        local ymo_bytes = ys.archive.read_file(arch_handle, stage_desc.ymo_path)
        if ymo_bytes then
            local m_handle, info = ys.ymo.load_from_memory(ymo_bytes, get_filename(stage_desc.ymo_path))
            if m_handle then
                scene.base_model_handle = m_handle
                scene.base_info = info
                scene.total_triangles = scene.total_triangles + (info.total_triangles or 0)
                scene.total_vertices = scene.total_vertices + (info.total_vertices or 0)

                if info.bounds then
                    scene.bounds = {
                        min = { x = info.bounds.min_x, y = info.bounds.min_y, z = info.bounds.min_z },
                        max = { x = info.bounds.max_x, y = info.bounds.max_y, z = info.bounds.max_z },
                    }
                    scene.center = { x = info.bounds.center_x, y = info.bounds.center_y, z = info.bounds.center_z }
                    scene.radius = info.bounds.radius or 30.0
                end

                stage_loader.resolve_and_bind_textures(arch_handle, m_handle, info, stage_dir)
            end
        end
    end

    -- 2. Load Placed Props / Objects from SOB
    if stage_desc.sob_path then
        local sob_bytes = ys.archive.read_file(arch_handle, stage_desc.sob_path)
        if sob_bytes then
            local placed_objs = ys.sob.parse_from_memory(sob_bytes)
            local loaded_prop_models = {} -- model_path -> { handle, info }

            for _, pobj in ipairs(placed_objs) do
                local prop_entry = {
                    index = pobj.index,
                    name = pobj.name,
                    filename = pobj.filename,
                    model_path = pobj.model_path,
                    position = pobj.pos,
                    rotation = pobj.rot,
                    scale = pobj.scale,
                    is_door_trigger = pobj.is_door_trigger,
                    visible = true,
                    model_handle = nil,
                    info = nil,
                }

                local norm_mpath = normalize_path(pobj.model_path)
                local p_fname = (pobj.filename or ""):lower()
                local p_name = (pobj.name or ""):lower()
                local base_fname = get_filename(stage_desc.ymo_path or ""):lower()
                local is_base_ref = (p_fname == base_fname) or (p_name == stage_desc.stage_id:lower())
                if is_base_ref and scene.base_model_handle then
                    prop_entry.model_handle = scene.base_model_handle
                    prop_entry.info = scene.base_info
                    loaded_prop_models[norm_mpath] = { handle = scene.base_model_handle, info = scene.base_info }
                elseif not loaded_prop_models[norm_mpath] then
                    local prop_bytes = ys.archive.read_file(arch_handle, norm_mpath)
                    if not prop_bytes then
                        local candidates = {
                            "map/mapobj/" .. pobj.name .. "/" .. pobj.filename,
                            "map/mapobj/" .. pobj.filename,
                            "mapobj/" .. pobj.name .. "/" .. pobj.filename,
                            stage_dir .. "/" .. pobj.filename,
                        }
                        for _, cand in ipairs(candidates) do
                            prop_bytes = ys.archive.read_file(arch_handle, cand)
                            if prop_bytes then
                                norm_mpath = cand
                                break
                            end
                        end
                    end

                    if prop_bytes then
                        local pm_handle, pinfo = ys.ymo.load_from_memory(prop_bytes, pobj.filename)
                        if pm_handle then
                            stage_loader.resolve_and_bind_textures(arch_handle, pm_handle, pinfo, get_parent_dir(norm_mpath))
                            loaded_prop_models[norm_mpath] = { handle = pm_handle, info = pinfo }
                        end
                    end
                end

                if loaded_prop_models[norm_mpath] then
                    prop_entry.model_handle = loaded_prop_models[norm_mpath].handle
                    prop_entry.info = loaded_prop_models[norm_mpath].info
                    scene.total_triangles = scene.total_triangles + (prop_entry.info.total_triangles or 0)
                end

                if prop_entry.is_door_trigger then
                    table.insert(scene.door_triggers, prop_entry)
                else
                    table.insert(scene.placed_props, prop_entry)
                end
            end
        end
    end

    -- 3. Load Collision Geometry (YCO)
    if stage_desc.coll_s_path then
        local bytes = ys.archive.read_file(arch_handle, stage_desc.coll_s_path)
        if bytes then
            local handle, info = ys.yco.load_from_memory(bytes, "walkable", get_filename(stage_desc.coll_s_path))
            if handle then scene.coll_walkable = { handle = handle, info = info } end
        end
    end

    if stage_desc.coll_w_path then
        local bytes = ys.archive.read_file(arch_handle, stage_desc.coll_w_path)
        if bytes then
            local handle, info = ys.yco.load_from_memory(bytes, "wall", get_filename(stage_desc.coll_w_path))
            if handle then scene.coll_wall = { handle = handle, info = info } end
        end
    end

    if stage_desc.coll_c_path then
        local bytes = ys.archive.read_file(arch_handle, stage_desc.coll_c_path)
        if bytes then
            local handle, info = ys.yco.load_from_memory(bytes, "camera", get_filename(stage_desc.coll_c_path))
            if handle then scene.coll_camera = { handle = handle, info = info } end
        end
    end

    -- 3b. Load Origin Collision Mesh (Stage_.YMO companion)
    if stage_desc.coll_mesh_path then
        local bytes = ys.archive.read_file(arch_handle, stage_desc.coll_mesh_path)
        if bytes then
            local handle, info = ys.ymo.load_from_memory(bytes, get_filename(stage_desc.coll_mesh_path))
            if handle then
                scene.coll_origin_mesh = { handle = handle, info = info }
            end
        end
    end
    -- 4. Load Camera Metadata (SCM)
    if stage_desc.scm_path then
        local bytes = ys.archive.read_file(arch_handle, stage_desc.scm_path)
        if bytes then
            local scm = ys.scm.parse_from_memory(bytes)
            if scm and scm.valid then
                scene.scm_camera = scm
            end
        end
    end

    -- 5. Compute Comprehensive Stage World Bounding Box & Center
    local wb_min = { x = 1e9, y = 1e9, z = 1e9 }
    local wb_max = { x = -1e9, y = -1e9, z = -1e9 }
    local has_wb = false

    if scene.base_model_handle and scene.base_info and scene.base_info.bounds then
        local b = scene.base_info.bounds
        wb_min.x = math.min(wb_min.x, b.min_x)
        wb_min.y = math.min(wb_min.y, b.min_y)
        wb_min.z = math.min(wb_min.z, b.min_z)
        wb_max.x = math.max(wb_max.x, b.max_x)
        wb_max.y = math.max(wb_max.y, b.max_y)
        wb_max.z = math.max(wb_max.z, b.max_z)
        has_wb = true
    end

    for _, prop in ipairs(scene.placed_props) do
        if prop.info and prop.info.bounds then
            local b = prop.info.bounds
            local p = prop.position
            local s = prop.scale
            wb_min.x = math.min(wb_min.x, p.x + b.min_x * s.x)
            wb_min.y = math.min(wb_min.y, p.y + b.min_y * s.y)
            wb_min.z = math.min(wb_min.z, p.z + b.min_z * s.z)
            wb_max.x = math.max(wb_max.x, p.x + b.max_x * s.x)
            wb_max.y = math.max(wb_max.y, p.y + b.max_y * s.y)
            wb_max.z = math.max(wb_max.z, p.z + b.max_z * s.z)
            has_wb = true
        end
    end

    for _, coll in ipairs({ scene.coll_walkable, scene.coll_wall, scene.coll_camera }) do
        if coll and coll.info and coll.info.bounds then
            local b = coll.info.bounds
            wb_min.x = math.min(wb_min.x, b.min_x)
            wb_min.y = math.min(wb_min.y, b.min_y)
            wb_min.z = math.min(wb_min.z, b.min_z)
            wb_max.x = math.max(wb_max.x, b.max_x)
            wb_max.y = math.max(wb_max.y, b.max_y)
            wb_max.z = math.max(wb_max.z, b.max_z)
            has_wb = true
        end
    end

    if has_wb and wb_min.x <= wb_max.x then
        scene.bounds = { min = wb_min, max = wb_max }
        scene.center = {
            x = (wb_min.x + wb_max.x) * 0.5,
            y = (wb_min.y + wb_max.y) * 0.5,
            z = (wb_min.z + wb_max.z) * 0.5,
        }
        local dx = wb_max.x - scene.center.x
        local dy = wb_max.y - scene.center.y
        local dz = wb_max.z - scene.center.z
        scene.radius = math.max(3.0, math.sqrt(dx*dx + dy*dy + dz*dz))
    end

    stage_loader.current_scene = scene
    return scene
end

function stage_loader.render_scene(scene, opts)
    if not scene then return end

    opts = opts or {}
    local show_textures = (opts.textures ~= false)
    local show_wireframe = (opts.wireframe == true)
    local show_vert_lighting = (opts.vertex_lighting ~= false)
    local show_props = (opts.props ~= false)
    local show_colliders = (opts.colliders == true)
    local show_doors = (opts.door_triggers == true)

    -- 1. Draw Base Map Model (if not already drawn via placed props)
    local base_drawn_in_props = false
    for _, prop in ipairs(scene.placed_props) do
        if prop.model_handle == scene.base_model_handle then
            base_drawn_in_props = true
            break
        end
    end
    if scene.base_model_handle and not base_drawn_in_props then
        ys.ymo.draw(scene.base_model_handle, 0, 0, 0, 0, 0, 0, 1, 1, 1, 255, 255, 255, 255, show_wireframe, not show_textures, show_vert_lighting)
    end
    if show_props then
        for _, prop in ipairs(scene.placed_props) do
            if prop.visible and prop.model_handle then
                local p, r, s = prop.position, prop.rotation, prop.scale
                ys.ymo.draw(prop.model_handle, p.x, p.y, p.z, r.x, r.y, r.z, s.x, s.y, s.z, 255, 255, 255, 255, show_wireframe, not show_textures, show_vert_lighting)
            end
        end
    end

    -- 3. Draw Door Triggers (Translucent Gold/Yellow with bounds outline)
    if show_doors then
        for _, door in ipairs(scene.door_triggers) do
            if door.visible and door.model_handle then
                local p, r, s = door.position, door.rotation, door.scale
                -- Translucent amber/gold tint
                ys.ymo.draw(door.model_handle, p.x, p.y, p.z, r.x, r.y, r.z, s.x, s.y, s.z, 245, 180, 20, 140, false, true)
                -- Also draw wireframe edges for clarity
                ys.ymo.draw(door.model_handle, p.x, p.y, p.z, r.x, r.y, r.z, s.x, s.y, s.z, 255, 220, 80, 240, true, true)
            end
        end
    end

    -- 4. Draw Colliders (Translucent Green/Orange/Cyan)
    if show_colliders then
        if scene.coll_walkable and scene.coll_walkable_visible ~= false then
            ys.yco.draw(scene.coll_walkable.handle, 0, 0, 0, 0, 0, 0, 1, 1, 1, 26, 230, 102, 110, show_wireframe)
        end
        if scene.coll_wall and scene.coll_wall_visible ~= false then
            ys.yco.draw(scene.coll_wall.handle, 0, 0, 0, 0, 0, 0, 1, 1, 1, 255, 102, 26, 110, show_wireframe)
        end
        if scene.coll_camera and scene.coll_camera_visible ~= false then
            ys.yco.draw(scene.coll_camera.handle, 0, 0, 0, 0, 0, 0, 1, 1, 1, 26, 153, 255, 110, show_wireframe)
        end
        if scene.coll_origin_mesh and scene.coll_origin_mesh_visible ~= false then
            -- Draw Origin collision mesh in translucent collision style with wireframe overlay
            ys.ymo.draw(scene.coll_origin_mesh.handle, 0, 0, 0, 0, 0, 0, 1, 1, 1, 26, 230, 102, 120, false, true, false)
            ys.ymo.draw(scene.coll_origin_mesh.handle, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 240, 180, 220, true, true, false)
        end
    end
end

function stage_loader.unload_current_stage()
    local scene = stage_loader.current_scene
    if not scene then return end

    local unloaded_handles = {}

    if scene.base_model_handle and not unloaded_handles[scene.base_model_handle] then
        ys.ymo.unload(scene.base_model_handle)
        unloaded_handles[scene.base_model_handle] = true
    end

    for _, prop in ipairs(scene.placed_props or {}) do
        if prop.model_handle and not unloaded_handles[prop.model_handle] then
            ys.ymo.unload(prop.model_handle)
            unloaded_handles[prop.model_handle] = true
        end
    end
    for _, door in ipairs(scene.door_triggers or {}) do
        if door.model_handle and not unloaded_handles[door.model_handle] then
            ys.ymo.unload(door.model_handle)
            unloaded_handles[door.model_handle] = true
        end
    end

    local unloaded_coll = {}
    if scene.coll_walkable and not unloaded_coll[scene.coll_walkable.handle] then
        ys.yco.unload(scene.coll_walkable.handle)
        unloaded_coll[scene.coll_walkable.handle] = true
    end
    if scene.coll_wall and not unloaded_coll[scene.coll_wall.handle] then
        ys.yco.unload(scene.coll_wall.handle)
        unloaded_coll[scene.coll_wall.handle] = true
    end
    if scene.coll_camera and not unloaded_coll[scene.coll_camera.handle] then
        ys.yco.unload(scene.coll_camera.handle)
        unloaded_coll[scene.coll_camera.handle] = true
    end
    if scene.coll_origin_mesh and not unloaded_handles[scene.coll_origin_mesh.handle] then
        ys.ymo.unload(scene.coll_origin_mesh.handle)
        unloaded_handles[scene.coll_origin_mesh.handle] = true
    end

    stage_loader.current_scene = nil
end

function stage_loader.clear_texture_cache()
    for _, tex_id in pairs(stage_loader.texture_cache) do
        ys.dds.unload_texture(tex_id)
    end
    stage_loader.texture_cache = {}
end

return stage_loader
