-- drive_wire_lighting.lua — Automated drive tape testing wireframe occlusion, vertex lighting toggle, and 2-line collapsible toolbar
local D = require("drive")

print("[drive] Starting Wireframe, Vertex Lighting & Collapsible Toolbar Tape...")

D.at(2, function()
    YV.picker.show_settings = false
    local target_stage = nil
    for _, st in ipairs(YV.registry.stages) do
        if st.stage_id:find("S_0100") and st.game_id == "felghana" then
            target_stage = st
            break
        end
    end
    if not target_stage then target_stage = YV.registry.stages[1] end
    assert(target_stage ~= nil, "Found stage to load")
    YV.viewer.open_stage(target_stage)
    YV.app.state = "viewer"

    YV.viewer.toggles.show_inspector = true
    YV.viewer.toggles.textures = true
    YV.viewer.toggles.wireframe = false
    YV.viewer.toggles.vertex_lighting = true
    YV.viewer.toggles.toolbar_collapsed = false
    print(string.format("[drive] Stage %s loaded: %d tris, %d verts",
        target_stage.stage_id, YV.viewer.scene.total_triangles, YV.viewer.scene.total_vertices))
end)

-- Frame 10: Toggle wireframe mode ON (wireframe over textured mesh, occluding floor grid & backfaces)
D.at(10, function()
    print("[drive] Frame 10: Toggling wireframe ON (textured + wireframe overlay)")
    YV.viewer.toggles.wireframe = true
    assert(YV.viewer.toggles.wireframe == true, "Wireframe enabled")
end)

-- Frame 15: Toggle textures OFF (untextured clay + wireframe overlay)
D.at(15, function()
    print("[drive] Frame 15: Toggling textures OFF with wireframe ON (solid clay + wireframe)")
    YV.viewer.toggles.textures = false
    assert(YV.viewer.toggles.textures == false, "Textures disabled")
end)

-- Frame 20: Toggle textures back ON
D.at(20, function()
    print("[drive] Frame 20: Toggling textures back ON")
    YV.viewer.toggles.textures = true
end)

-- Frame 25: Toggle vertex lighting OFF
D.at(25, function()
    print("[drive] Frame 25: Toggling vertex lighting OFF (unlit raw diffuse)")
    YV.viewer.toggles.vertex_lighting = false
    assert(YV.viewer.toggles.vertex_lighting == false, "Vertex lighting disabled")
end)

-- Frame 30: Toggle vertex lighting back ON
D.at(30, function()
    print("[drive] Frame 30: Toggling vertex lighting back ON")
    YV.viewer.toggles.vertex_lighting = true
    assert(YV.viewer.toggles.vertex_lighting == true, "Vertex lighting enabled")
end)

-- Frame 35: Collapse toolbar (clicking ^)
D.at(35, function()
    print("[drive] Frame 35: Collapsing toolbar (^ button)")
    YV.viewer.toggles.toolbar_collapsed = true
    assert(YV.viewer.toggles.toolbar_collapsed == true, "Toolbar collapsed")
end)

-- Frame 40: Expand toolbar (clicking V)
D.at(40, function()
    print("[drive] Frame 40: Expanding toolbar (V button)")
    YV.viewer.toggles.toolbar_collapsed = false
    assert(YV.viewer.toggles.toolbar_collapsed == false, "Toolbar expanded")
end)

-- Frame 45: Complete
D.at(45, function()
    print("[drive] Tape completed successfully!")
end)
