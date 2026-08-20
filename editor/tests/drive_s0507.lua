-- drive_s0507.lua - Test Ys VI S_0507 and Felghana S_1000 in 3D viewer
local D = require("drive")

print("[drive] Starting S_0507 & S_1000 verification tape...")

D.at(2, function()
    YV.picker.show_settings = false
    local s0507_stage = nil
    for _, st in ipairs(YV.registry.stages) do
        if st.game_id == "ys6" and st.stage_id == "S_0507" then
            s0507_stage = st
            break
        end
    end
    assert(s0507_stage ~= nil, "Found Ys VI S_0507 stage in registry")
    YV.viewer.open_stage(s0507_stage)
    YV.app.state = "viewer"
    YV.viewer.toggles.show_inspector = true
    YV.viewer.toggles.textures = true
    YV.viewer.toggles.wireframe = false
    print(string.format("[drive] S_0507 loaded: %d total tris, %d total verts",
        YV.viewer.scene.total_triangles, YV.viewer.scene.total_vertices))
end)

-- Orbit camera
D.drag(10, 500, 400, 620, 360, 8, 2)
