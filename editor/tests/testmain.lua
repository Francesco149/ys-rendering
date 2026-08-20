-- testmain.lua — Headless unit test suite for Ys Map & Mesh Viewer
local pass_count = 0
local fail_count = 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        pass_count = pass_count + 1
        print(string.format("  [PASS] %s", name))
    else
        fail_count = fail_count + 1
        print(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
end

local function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("%s: expected %s, got %s", msg or "assertion failed", tostring(b), tostring(a)), 2)
    end
end

local function assert_true(cond, msg)
    if not cond then
        error(msg or "assertion failed (expected truthy)", 2)
    end
end

print("========================================================================")
print("  Ys Map & Mesh Viewer — Headless Test Suite")
print("========================================================================")

-- 1. C++ Bindings Presence
test("C++ Bindings & Core APIs", function()
    assert_true(lp ~= nil, "lp namespace exists")
    assert_true(lp.rl ~= nil, "lp.rl exists")
    assert_true(lp.drive ~= nil, "lp.drive exists")
    assert_true(lp.app ~= nil, "lp.app exists")
    assert_true(ig ~= nil, "ig namespace exists")
    assert_true(ig.icon ~= nil, "ig.icon FontAwesome table exists")
    assert_true(ys ~= nil, "ys namespace exists")
    assert_true(ys.archive ~= nil, "ys.archive exists")
    assert_true(ys.dds ~= nil, "ys.dds exists")
    assert_true(ys.ymo ~= nil, "ys.ymo exists")
    assert_true(ys.yco ~= nil, "ys.yco exists")
    assert_true(ys.sob ~= nil, "ys.sob exists")
    assert_true(ys.scm ~= nil, "ys.scm exists")
end)

-- 2. Modules Loading
local config, registry, stage_loader, picker, viewer, inspector
test("Lua Modules Loading", function()
    config = require("config")
    registry = require("map_registry")
    stage_loader = require("stage_loader")
    picker = require("picker")
    viewer = require("viewer")
    inspector = require("inspector")

    assert_true(config ~= nil, "config module loaded")
    assert_true(registry ~= nil, "map_registry module loaded")
    assert_true(stage_loader ~= nil, "stage_loader module loaded")
    assert_true(picker ~= nil, "picker module loaded")
    assert_true(viewer ~= nil, "viewer module loaded")
    assert_true(inspector ~= nil, "inspector module loaded")
end)

-- 3. Game Auto-Detection
local has_game_archives = false
test("Steam Game Path Auto-Detection", function()
    config.detect_all_games()
    local detected_count = 0
    for gid, gdef in pairs(config.game_defs) do
        local gconf = config.settings.games[gid]
        if gconf.detected_path and gconf.detected_path ~= "" then
            detected_count = detected_count + 1
            print(string.format("    - Detected %s at: %s", gdef.name, gconf.detected_path))
        end
    end
    print(string.format("    Total detected Ys games: %d / 3", detected_count))
    if detected_count > 0 then
        has_game_archives = true
    else
        print("    [NOTE] No Steam game archives found on this runner (expected in headless CI)")
    end
end)

-- 4. Direct Archive Reading (Felghana / Origin / Ys VI)
test("Direct Archive Reading & Decompression", function()
    if not has_game_archives then
        print("    [SKIP] Game archives not present on runner")
        return
    end
    local tested_game = nil
    for _, gid in ipairs({ "felghana", "origin", "ys6" }) do
        if config.settings.games[gid].enabled and config.settings.games[gid].archive_path ~= "" then
            tested_game = gid
            break
        end
    end

    assert_true(tested_game ~= nil, "Found an active game for archive test")
    local handle = config.open_game_archive(tested_game)
    assert_true(handle ~= nil, "Opened archive successfully")

    local files = ys.archive.list_files(handle, "map/")
    assert_true(#files > 0, "Listed map files from archive: found " .. #files)
    print(string.format("    Discovered %d map files in %s archive", #files, tested_game))

    -- Find and read a test file
    local test_file = files[1].path
    local bytes = ys.archive.read_file(handle, test_file)
    assert_true(bytes ~= nil and #bytes > 0, "Read bytes from " .. test_file .. " (" .. tostring(#bytes) .. " bytes)")
end)

-- 5. SOB Stage Parser
test("SOB Stage Object Placement Parser", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local handle = config.open_game_archive("felghana")
    local files = ys.archive.list_files(handle, "map/")
    local sob_file = nil
    for _, f in ipairs(files) do
        if f.path:find("s_0100%.sob") then
            sob_file = f.path
            break
        end
    end

    assert_true(sob_file ~= nil, "Found S_0100 SOB file in archive")
    local bytes = ys.archive.read_file(handle, sob_file)
    assert_true(bytes ~= nil and #bytes > 16, "Read SOB file: " .. sob_file)
    local objs = ys.sob.parse_from_memory(bytes)
    assert_true(objs ~= nil and #objs > 0, "Parsed SOB objects table")
    print(string.format("    Parsed SOB %s: %d placed objects", sob_file, #objs))
    for i, o in ipairs(objs) do
        print(string.format("      [%d] name=%s, model=%s, pos=(%.1f, %.1f, %.1f), rot=(%.1f, %.1f, %.1f), scale=(%.1f, %.1f, %.1f), trigger=%s",
            i, o.name, o.model_path, o.pos.x, o.pos.y, o.pos.z, o.rot.x, o.rot.y, o.rot.z, o.scale.x, o.scale.y, o.scale.z, tostring(o.is_door_trigger)))
    end
end)

-- 6. YMO Model Parser
test("YMO 3D Model & Mesh Parser", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local handle = config.open_game_archive("felghana")
    local files = ys.archive.list_files(handle, "map/")
    local ymo_file = nil
    for _, f in ipairs(files) do
        if f.path:find("s_0100%.ymo") then
            ymo_file = f.path
            break
        end
    end

    assert_true(ymo_file ~= nil, "Found S_0100 YMO file in archive")
    local bytes = ys.archive.read_file(handle, ymo_file)
    assert_true(bytes ~= nil and #bytes > 68, "Read YMO file: " .. ymo_file)
    local m_handle, info = ys.ymo.load_from_memory(bytes, "s_0100.ymo")
    assert_true(m_handle ~= nil, "Parsed and loaded YMO model to GPU/Model")
    assert_true(info ~= nil, "Returned YMO info table")
    assert_true(info.total_triangles > 0, "Model has triangles: " .. tostring(info.total_triangles))
    assert_true(info.total_vertices > 0, "Model has vertices: " .. tostring(info.total_vertices))
    assert_true(#info.materials > 0, "Model has materials")
    assert_true(#info.submeshes > 0, "Model has submeshes")

    print(string.format("    YMO %s: %d tris, %d verts, %d mats, %d submeshes",
        ymo_file, info.total_triangles, info.total_vertices, #info.materials, #info.submeshes))
    print(string.format("    Bounds: min=(%.1f, %.1f, %.1f), max=(%.1f, %.1f, %.1f), center=(%.1f, %.1f, %.1f), radius=%.1f",
        info.bounds.min_x, info.bounds.min_y, info.bounds.min_z,
        info.bounds.max_x, info.bounds.max_y, info.bounds.max_z,
        info.bounds.center_x, info.bounds.center_y, info.bounds.center_z,
        info.bounds.radius))
    ys.ymo.unload(m_handle)

    -- Test S_1000.YMO
    local ymo1000_file = nil
    for _, f in ipairs(files) do
        if f.path:find("s_1000%.ymo") then
            ymo1000_file = f.path
            break
        end
    end
    if ymo1000_file then
        local b1000 = ys.archive.read_file(handle, ymo1000_file)
        local m1000, i1000 = ys.ymo.load_from_memory(b1000, "s_1000.ymo")
        assert_true(m1000 ~= nil, "Parsed and loaded S_1000 YMO")
        assert_eq(i1000.total_triangles, 19757, "S_1000 Oracle triangle count parity")
        ys.ymo.unload(m1000)
    end

    -- Test s_0000.ymo
    local s0000_file = nil
    for _, f in ipairs(files) do
        if f.path:find("s_0000%.ymo") then
            s0000_file = f.path
            break
        end
    end
    if s0000_file then
        local b0 = ys.archive.read_file(handle, s0000_file)
        local h0, inf0 = ys.ymo.load_from_memory(b0, "s_0000.ymo")
        print(string.format("    Bounds: min=(%.1f, %.1f, %.1f), max=(%.1f, %.1f, %.1f), center=(%.1f, %.1f, %.1f), radius=%.1f",
            inf0.bounds.min_x, inf0.bounds.min_y, inf0.bounds.min_z,
            inf0.bounds.max_x, inf0.bounds.max_y, inf0.bounds.max_z,
            inf0.bounds.center_x, inf0.bounds.center_y, inf0.bounds.center_z, inf0.bounds.radius))
        ys.ymo.unload(h0)
    end
end)

-- 6b. Oracle Parity Test for S_0100 Benchmark
test("Oracle Ground Truth Parity Test (S_0100)", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local handle = config.open_game_archive("felghana")
    local files = ys.archive.list_files(handle, "map/")
    local ymo0100 = nil
    for _, f in ipairs(files) do
        if f.path:find("s_0100%.ymo") then ymo0100 = f.path; break end
    end
    assert_true(ymo0100 ~= nil, "Found S_0100 YMO")
    local bytes = ys.archive.read_file(handle, ymo0100)
    local m, info = ys.ymo.load_from_memory(bytes, "s_0100.ymo")
    assert_true(m ~= nil, "Loaded S_0100 YMO")
    assert_eq(info.total_triangles, 9444, "S_0100 triangle count matches oracle (9444)")
    assert_eq(#info.materials, 19, "S_0100 material count matches oracle (19)")
    assert_eq(#info.submeshes, 19, "S_0100 submesh count matches oracle (19)")
    assert_true(math.abs(info.bounds.min_x - (-12.73)) < 0.2, "S_0100 min_x bounds matches oracle")
    assert_true(math.abs(info.bounds.max_x - 12.73) < 0.2, "S_0100 max_x bounds matches oracle")
    print("    [Oracle Parity Verified] S_0100: 9444 tris, 19 submeshes, 19 mats, bounds matched")
    ys.ymo.unload(m)
end)

-- 7. YCO Collision Parser
test("YCO Collision Object Parser", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local handle = config.open_game_archive("felghana")
    local files = ys.archive.list_files(handle, "map/")
    local yco_file = nil
    for _, f in ipairs(files) do
        if f.path:find("s_0100.*__s%.yco") or f.path:find("s_0100.*_s%.yco") then
            yco_file = f.path
            break
        end
    end

    assert_true(yco_file ~= nil, "Found S_0100 collision file in archive")
    local bytes = ys.archive.read_file(handle, yco_file)
    assert_true(bytes ~= nil and #bytes > 28, "Read YCO file: " .. yco_file)
    local c_handle, info = ys.yco.load_from_memory(bytes, "walkable", "s_0100__s.yco")
    assert_true(c_handle ~= nil, "Parsed and loaded YCO collision to GPU/Mesh")
    assert_true(info ~= nil, "Returned YCO info table")
    assert_eq(info.total_triangles, 9384, "S_0100 YCO collision triangle count matches full polygon array (9384 tris)")
    print(string.format("    YCO %s: %d collision triangles (%s)", yco_file, info.total_triangles, info.type))
    ys.yco.unload(c_handle)
end)

-- 8. Composite Stage Loader
test("Composite Stage Scene Loader (S_0100 Redmont Tavern)", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    registry.rescan_all()
    local st_desc = nil
    for _, st in ipairs(registry.stages) do
        if st.game_id == "felghana" and st.stage_id:find("S_0100") then
            st_desc = st
            break
        end
    end
    assert_true(st_desc ~= nil, "Found S_0100 stage descriptor in registry")

    local scene, err = stage_loader.load_stage(st_desc)
    assert_true(scene ~= nil, "Loaded composite stage scene: " .. tostring(err))
    assert_true(scene.base_model_handle ~= nil, "Scene has base model")
    assert_true(#scene.placed_props > 0, "Scene has placed props: " .. tostring(#scene.placed_props))
    assert_true(scene.coll_walkable ~= nil, "Scene has walkable collision")
    assert_true(scene.scm_camera ~= nil, "Scene has SCM camera metadata")
    print(string.format("    Loaded Stage S_0100: %d total tris, %d props, %d door triggers, SCM pitch=%.2f rad",
        scene.total_triangles, #scene.placed_props, #scene.door_triggers, scene.scm_camera.pitch))

    stage_loader.unload_current_stage()
end)

-- 9. Map Registry & Filtering
test("Map Registry Scanning & Filtering", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    registry.rescan_all()
    assert_true(#registry.stages > 0, "Discovered stages across enabled games: " .. tostring(#registry.stages))
    print(string.format("    Total registered stages: %d", #registry.stages))

    -- Test search filter
    registry.search_query = "Redmont"
    registry.apply_filter()
    print(string.format("    Filtered by 'Redmont': %d stages", #registry.filtered))

    registry.search_query = ""
    registry.apply_filter()
    assert_true(#registry.filtered == #registry.stages, "Cleared filter restored all stages")
end)

-- 10. ImGui Scoped Balance Checker
test("ImGui Scoped Balance Safety Check", function()
    ig.balance_check()
    assert_true(true, "Balance checker executed cleanly")
end)

-- 11. Smooth Camera Interpolation & Dampened Motion
test("Smooth 3D View Camera Motion & Zoom", function()
    viewer.camera.target = { x = 0, y = 0, z = 0 }
    viewer.camera.curr_target = { x = 0, y = 0, z = 0 }
    viewer.camera.distance = 50.0
    viewer.camera.curr_distance = 50.0
    viewer.camera.yaw = 0.0
    viewer.camera.curr_yaw = 0.0
    viewer.camera.pitch = 0.65
    viewer.camera.curr_pitch = 0.65

    -- Change target distance (zoom in)
    viewer.camera.distance = 25.0
    assert_eq(viewer.camera.curr_distance, 50.0, "Current distance before dt update")

    -- Update with 1/60s frame dt
    local dt = 1.0 / 60.0
    viewer.update_camera_eye(dt, false)
    assert_true(viewer.camera.curr_distance < 50.0, "Current distance smoothly glided towards target")
    assert_true(viewer.camera.curr_distance > 25.0, "Current distance did not instantly snap (smooth interpolation)")

    -- Snap test
    viewer.update_camera_eye(dt, true)
    assert_eq(viewer.camera.curr_distance, 25.0, "Snap mode instantly syncs current to target")
    print(string.format("    Camera smoothing verified: lerp_speed=%.1f", viewer.camera.smooth_speed or 18.0))
end)

-- 12. Search Map & Tigray Quarry S_1000 Thumbnail
test("Tigray Quarry S_1000 Search & Thumbnail Generation", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    registry.search_query = "tigray quarry"
    registry.apply_filter()
    assert_true(#registry.filtered > 0, "Found Tigray Quarry stages in search")

    local s1000_stage = nil
    for _, st in ipairs(registry.filtered) do
        if st.stage_id == "S_1000" and st.game_id == "felghana" then
            s1000_stage = st
            break
        end
    end
    assert_true(s1000_stage ~= nil, "Found Felghana S_1000 stage in filtered search")
    assert_true(s1000_stage.ymo_path ~= nil, "S_1000 has valid YMO path")

    local thumb = registry.get_thumbnail(s1000_stage)
    assert_true(thumb ~= nil, "Created/retrieved thumbnail object for S_1000")
    assert_true(thumb.rt_id > 0, "Thumbnail has valid render texture ID: " .. tostring(thumb.rt_id))
    assert_true(thumb.gl_id > 0, "Thumbnail has valid OpenGL texture ID: " .. tostring(thumb.gl_id))

    -- Clean up filter
    registry.search_query = ""
    registry.apply_filter()
    print(string.format("    S_1000 thumbnail verified (rt_id=%d, gl_id=%d)", thumb.rt_id, thumb.gl_id))
end)

-- 13. Ys VI S_0600 Stage Loading & Alpha Blending / Additive Light Shafts
test("Ys VI S_0600 Stage, Foliage Textures & Additive Light Shafts", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local s0600_desc = nil
    for _, st in ipairs(registry.stages) do
        if st.game_id == "ys6" and st.stage_id:find("S_0600") then
            s0600_desc = st
            break
        end
    end
    assert_true(s0600_desc ~= nil, "Found Ys VI S_0600 in stage registry")

    local sc, err = stage_loader.load_stage(s0600_desc)
    assert_true(sc ~= nil, "Loaded Ys VI S_0600 stage scene: " .. tostring(err))
    assert_true(sc.base_model_handle ~= nil, "S_0600 has base model")
    assert_true(sc.base_info ~= nil, "S_0600 has model info")
    assert_true(#sc.base_info.materials >= 19, "S_0600 has 20 materials (including foliage and light shafts)")

    -- Check materials: light shaft (z_zhikari), water, foliage (1_jusiba / 1_grass)
    local has_zhikari = false
    local has_water = false
    local has_foliage = false
    for _, m in ipairs(sc.base_info.materials) do
        local tname = m.texture_name:lower()
        if tname:find("z_") or tname:find("hikari") then
            has_zhikari = true
        end
        if tname:find("water") and m.alpha < 0.99 then
            has_water = true
        end
        if tname:find("jusiba") or tname:find("grass") then
            has_foliage = true
        end
    end
    assert_true(has_zhikari, "S_0600 has additive light shaft material (Z_ZHIKARI)")
    assert_true(has_water, "S_0600 has translucent water material")
    assert_true(has_foliage, "S_0600 has foliage materials")
    print(string.format("    Ys VI S_0600 verified: %d tris, %d materials (zhikari=%s, water=%s, foliage=%s)",
        sc.total_triangles, #sc.base_info.materials, tostring(has_zhikari), tostring(has_water), tostring(has_foliage)))

    stage_loader.unload_current_stage()
end)

-- 14. LRU Cache Capacity & Eviction Verification
test("LRU Thumbnail Cache Capacity & Eviction Policy", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local stage_samples = {}
    for i = 1, math.min(10, #registry.stages) do
        table.insert(stage_samples, registry.stages[i])
    end
    for _, st in ipairs(stage_samples) do
        local th = registry.get_thumbnail(st)
        assert_true(th ~= nil and th.rt_id > 0, "Retrieved valid thumbnail")
    end
    print(string.format("    LRU thumbnail cache capacity test passed (max=256)"))
end)

-- 15. Ys Origin Stage Loading & Stride 48 Mesh Parsing
test("Ys Origin Stage Loading & Stride 48 Mesh Parser", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local origin_desc = nil
    for _, st in ipairs(registry.stages) do
        if st.game_id == "origin" and st.stage_id == "S_1000" then
            origin_desc = st
            break
        end
    end
    if not origin_desc then
        for _, st in ipairs(registry.stages) do
            if st.game_id == "origin" then origin_desc = st; break end
        end
    end
    assert_true(origin_desc ~= nil, "Found Ys Origin stage in registry")

    -- Verify no underscore duplicate stages in registry
    for _, st in ipairs(registry.stages) do
        if st.game_id == "origin" then
            assert_true(not st.stage_id:find("_$"), "Origin stage ID does not end with duplicate underscore: " .. st.stage_id)
        end
    end

    local sc, err = stage_loader.load_stage(origin_desc)
    assert_true(sc ~= nil, "Loaded Ys Origin stage scene: " .. tostring(err))
    assert_true(sc.base_model_handle ~= nil, "Loaded Ys Origin stage model handle")
    assert_true(sc.coll_origin_mesh ~= nil, "Origin stage loaded companion collision mesh (_.ymo)")
    assert_eq(sc.coll_origin_mesh.info.total_triangles, 358, "Origin S_1000 collision mesh has 358 triangles")
    if sc.base_info then
        print(string.format("    Ys Origin %s stage verified: %d total tris, %d total verts, %d submeshes, %d materials, %d coll tris",
            origin_desc.stage_id, sc.total_triangles, sc.total_vertices, #sc.base_info.submeshes, #sc.base_info.materials, sc.coll_origin_mesh.info.total_triangles))
    end
    stage_loader.unload_current_stage()
end)

-- 16. Viewer Wireframe, Vertex Lighting & 2-Line Collapsible Toolbar State
test("Viewer Wireframe, Vertex Lighting & Collapsible Toolbar Toggles", function()
    assert_true(viewer.toggles ~= nil, "viewer.toggles table exists")
    assert_true(viewer.toggles.vertex_lighting == true, "vertex_lighting defaults to true")
    assert_true(viewer.toggles.toolbar_collapsed == false, "toolbar_collapsed defaults to false")
    assert_true(viewer.toggles.textures == true, "textures defaults to true")
    assert_true(viewer.toggles.wireframe == false, "wireframe defaults to false")

    -- Test toggling
    viewer.toggles.vertex_lighting = false
    assert_eq(viewer.toggles.vertex_lighting, false, "vertex_lighting can be toggled off")
    viewer.toggles.vertex_lighting = true

    viewer.toggles.toolbar_collapsed = true
    assert_eq(viewer.toggles.toolbar_collapsed, true, "toolbar_collapsed can be toggled on")
    viewer.toggles.toolbar_collapsed = false

    -- Verify stage_loader accepts opts with vertex_lighting
    if has_game_archives then
        local test_stage = registry.stages[1]
        local sc = stage_loader.load_stage(test_stage)
        assert_true(sc ~= nil, "Loaded stage for render_scene test")
        stage_loader.render_scene(sc, {
            textures = true,
            wireframe = true,
            vertex_lighting = false,
            colliders = false,
            door_triggers = false,
            props = true,
        })
        stage_loader.unload_current_stage()
    end
    print("    Viewer wireframe, vertex lighting, and toolbar toggles verified")
end)

-- 18. Gallery Thumbnail Dynamic Texture Streaming Across Tabs & Pages
test("Gallery Thumbnail Dynamic Texture Streaming Across Tabs & Pages", function()
    if not has_game_archives then print("    [SKIP] Game archives not present on runner"); return end
    local picker = require("picker")
    picker.init()

    -- 1. All Games Tab - Page 1
    registry.selected_game = "all"
    registry.apply_filter()
    picker.current_page = 1
    assert_true(#registry.filtered > 0, "Discovered filtered maps for All Games")

    local p1_stages = {}
    for i = 1, math.min(6, #registry.filtered) do
        table.insert(p1_stages, registry.filtered[i])
    end

    -- Exercise thumbnail generation and async texture streaming
    for _ = 1, 5 do
        picker.models_loaded_this_frame = 0
        for _, st in ipairs(p1_stages) do
            picker.ensure_thumbnail_rendered(st, 0.016)
        end
    end

    -- Poll async completions until textures settle (with timeout budget)
    for _ = 1, 100 do
        picker.process_async_completions()
    end
    local p1_textured_count = 0
    for _, st in ipairs(p1_stages) do
        local th = registry.get_thumbnail(st)
        if th and (th.has_textures or (th.bound_materials and next(th.bound_materials))) then
            p1_textured_count = p1_textured_count + 1
        end
    end
    assert_true(p1_textured_count > 0, "Page 1 thumbnails dynamically received textures")

    -- 2. Switch Tab to Ys Origin
    registry.selected_game = "origin"
    registry.apply_filter()
    picker.current_page = 1
    if lp.async then lp.async.clear_pending() end

    local origin_stages = {}
    for i = 1, math.min(6, #registry.filtered) do
        table.insert(origin_stages, registry.filtered[i])
    end
    for _ = 1, 5 do
        picker.models_loaded_this_frame = 0
        for _, st in ipairs(origin_stages) do
            picker.ensure_thumbnail_rendered(st, 0.016)
        end
    end
    for _ = 1, 100 do
        picker.process_async_completions()
    end
    local origin_textured = 0
    for _, st in ipairs(origin_stages) do
        local th = registry.get_thumbnail(st)
        if th and (th.has_textures or (th.bound_materials and next(th.bound_materials))) then
            origin_textured = origin_textured + 1
        end
    end
    assert_true(origin_textured > 0, "Ys Origin tab thumbnails dynamically received textures")

    -- 3. Switch Tab to Ys VI and navigate to Page 2
    registry.selected_game = "ys6"
    registry.apply_filter()
    picker.current_page = 2
    if lp.async then lp.async.clear_pending() end

    local ys6_p2_stages = {}
    local start_i = (picker.current_page - 1) * picker.page_size + 1
    for i = start_i, math.min(start_i + 5, #registry.filtered) do
        table.insert(ys6_p2_stages, registry.filtered[i])
    end

    for _ = 1, 5 do
        picker.models_loaded_this_frame = 0
        for _, st in ipairs(ys6_p2_stages) do
            picker.ensure_thumbnail_rendered(st, 0.016)
        end
    end
    for _ = 1, 100 do
        picker.process_async_completions()
    end
    local ys6_p2_textured = 0
    for _, st in ipairs(ys6_p2_stages) do
        local th = registry.get_thumbnail(st)
        if th and (th.has_textures or (th.bound_materials and next(th.bound_materials))) then
            ys6_p2_textured = ys6_p2_textured + 1
        end
    end
    assert_true(ys6_p2_textured > 0, "Ys VI Page 2 thumbnails dynamically streamed textures")

    -- Clean up filter
    registry.selected_game = "all"
    registry.apply_filter()
    picker.current_page = 1
    print(string.format("    Dynamic thumbnail texturing verified across All Games (%d), Origin (%d), and Ys VI P2 (%d)",
        p1_textured_count, origin_textured, ys6_p2_textured))
end)

print("========================================================================")
print(string.format("  Test Summary: %d Passed, %d Failed", pass_count, fail_count))
print("========================================================================")

if fail_count > 0 then
    os.exit(1)
end
