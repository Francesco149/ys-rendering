-- drive_viewer.lua — Capture screenshot directly inside the 3D Viewer
local D = require("drive")

print("[drive] Starting Ys Viewer 3D view test tape...")

D.at(2, function()
    -- Dismiss settings modal if open
    YV.picker.show_settings = false
    -- Find S_1000 (Tigray Quarry Exterior Canyon)
    local target_stage = nil
    for _, st in ipairs(YV.registry.stages) do
        if st.stage_id:find("S_1000") and st.game_id == "felghana" then
            target_stage = st
            break
        end
    end
    if not target_stage then target_stage = YV.registry.stages[1] end
    assert(target_stage ~= nil, "Found stage to load")
    YV.viewer.open_stage(target_stage)
    YV.app.state = "viewer"

    -- Enable colliders and door triggers
    YV.viewer.toggles.colliders = true
    YV.viewer.toggles.door_triggers = true
    YV.viewer.toggles.show_inspector = true
    YV.viewer.toggles.in_game_camera = true
    YV.viewer.apply_in_game_camera()
end)

-- Orbit camera slightly for a dynamic diorama view
D.drag(5, 500, 400, 620, 360, 8, 2)
