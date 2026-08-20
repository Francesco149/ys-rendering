-- drive_s0600.lua - Test Ys VI S_0600 (Canaan Forest)
local D = require("drive")

print("[drive] Starting S_0600 Canaan Forest verification tape...")

D.at(2, function()
    YV.picker.show_settings = false
    local s0600_stage = nil
    for _, st in ipairs(YV.registry.stages) do
        if st.game_id == "ys6" and st.stage_id == "S_0600" then
            s0600_stage = st
            break
        end
    end
    assert(s0600_stage ~= nil, "Found Ys VI S_0600 stage in registry")
    YV.viewer.open_stage(s0600_stage)
    YV.app.state = "viewer"
    YV.viewer.toggles.show_inspector = true
    YV.viewer.toggles.textures = true
    YV.viewer.toggles.wireframe = false
    print(string.format("[drive] S_0600 loaded: %d total tris, %d total verts",
        YV.viewer.scene.total_triangles, YV.viewer.scene.total_vertices))
end)

-- Orbit camera to view trees, light shafts, and water
D.drag(10, 500, 400, 580, 360, 8, 2)
