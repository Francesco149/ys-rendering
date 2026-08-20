-- drive_smoke.lua — Headless input drive tape for Ys Map & Mesh Viewer
local D = require("drive")

print("[drive] Starting Ys Viewer interactive input drive tape...")

-- Frame 2: Ensure we are in map picker
D.at(2, function()
    print("[drive] Frame 2: Verifying Map Picker state")
    assert(YV.app.state == "picker", "App is in picker state")
    assert(#YV.registry.stages > 0, "Stages discovered in registry")
end)

-- Frame 5: Hover over the first card thumbnail to exercise 3D turntable rotation
D.at(5, function()
    print("[drive] Frame 5: Hovering over first stage card thumbnail")
    D.mouse(120, 150)
end)

-- Frame 10: Select stage S_0100 (Redmont Tavern)
D.at(10, function()
    print("[drive] Frame 10: Selecting stage S_0100")
    local s0100 = nil
    for _, st in ipairs(YV.registry.stages) do
        if st.stage_id:find("S_0100") then
            s0100 = st
            break
        end
    end
    assert(s0100 ~= nil, "Found S_0100 stage")
    YV.viewer.open_stage(s0100)
    YV.app.state = "viewer"
end)

-- Frame 15: Verify 3D Viewer is active and stage loaded
D.at(15, function()
    print("[drive] Frame 15: In 3D Viewer with stage loaded")
    assert(YV.app.state == "viewer", "App transitioned to viewer state")
    assert(YV.viewer.scene ~= nil, "Stage scene is active")
    assert(YV.viewer.scene.total_triangles > 0, "Scene has triangles")
end)

-- Frame 18-24: Middle-mouse drag to orbit camera
D.drag(18, 500, 400, 650, 350, 6, 2)

-- Frame 26: Toggle colliders ON
D.at(26, function()
    print("[drive] Frame 26: Toggling colliders ON")
    YV.viewer.toggles.colliders = true
end)

-- Frame 28: Toggle door triggers ON
D.at(28, function()
    print("[drive] Frame 28: Toggling door triggers ON")
    YV.viewer.toggles.door_triggers = true
end)

-- Frame 30: Toggle In-Game Camera ON
D.at(30, function()
    print("[drive] Frame 30: Toggling In-Game Camera")
    YV.viewer.toggles.in_game_camera = true
    YV.viewer.apply_in_game_camera()
end)

-- Frame 35: Focus camera
D.tap(35, D.Key.F)

-- Frame 38: Press Escape to return to picker
D.at(38, function()
    print("[drive] Frame 38: Pressing Escape to return to picker")
    YV.viewer.return_to_picker = true
end)

-- Frame 42: Assert return to picker & filter by 'quarry'
D.at(42, function()
    print("[drive] Frame 42: Returned to map picker, filtering by 'quarry'")
    assert(YV.app.state == "picker", "Successfully returned to map picker")
    YV.registry.search_query = "quarry"
    YV.registry.apply_filter()
    assert(#YV.registry.filtered > 0, "Filtered stages found")
end)

-- Frame 46: Pick S_1000 from filtered results
D.at(46, function()
    print("[drive] Frame 46: Loading second stage (S_1000)")
    local s1000 = nil
    for _, st in ipairs(YV.registry.filtered) do
        if st.stage_id:find("S_1000") then s1000 = st; break end
    end
    assert(s1000 ~= nil, "Found S_1000 in filtered list")
    YV.viewer.open_stage(s1000)
    YV.app.state = "viewer"
    YV.viewer.toggles.colliders = true
    YV.viewer.toggles.door_triggers = true
    YV.viewer.toggles.show_inspector = true
end)

-- Frame 52: Orbit camera in S_1000
D.drag(50, 500, 400, 620, 370, 4, 2)

-- Frame 56: Final assertions
D.at(56, function()
    print("[drive] Frame 56: In S_1000 viewer with inspector open")
    assert(YV.app.state == "viewer", "App is in viewer state")
    assert(YV.viewer.scene ~= nil, "S_1000 scene is active")
    assert(YV.viewer.scene.total_triangles > 0, "S_1000 has triangles")
    print("[drive] End-to-end interactive drive tape PASSED.")
end)
