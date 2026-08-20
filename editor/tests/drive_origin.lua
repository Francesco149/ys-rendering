-- drive_origin.lua — Test Ys Origin S_0004 in 3D Viewer (textured & wireframe)
local D = require("drive")

print("[drive] Starting Ys Origin S_0004 test tape...")

D.at(2, function()
    YV.picker.show_settings = false
    local s0004_stage = nil
    for _, st in ipairs(YV.registry.stages) do
        if st.game_id == "origin" and st.stage_id == "S_0004" then
            s0004_stage = st
            break
        end
    end
    assert(s0004_stage ~= nil, "Found Ys Origin S_0004 stage in registry")
    YV.viewer.open_stage(s0004_stage)
    YV.app.state = "viewer"
    YV.viewer.toggles.show_inspector = true
    YV.viewer.toggles.textures = true
    YV.viewer.toggles.wireframe = false
    print(string.format("[drive] S_0004 loaded: %d tris, %d vertices",
        YV.viewer.scene.total_triangles, YV.viewer.scene.total_vertices))
end)

-- Frame 10: Orbit camera
D.drag(10, 500, 400, 620, 360, 8, 2)

-- Frame 25: Toggle wireframe mode
D.at(25, function()
    print("[drive] Toggling wireframe mode ON with backface culling & azure color")
    YV.viewer.toggles.wireframe = true
end)

-- Frame 35: Toggle wireframe mode back to textured
D.at(35, function()
    print("[drive] Toggling textured mode back ON")
    YV.viewer.toggles.wireframe = false
    YV.viewer.toggles.textures = true
end)
