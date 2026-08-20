-- main.lua — Ys Map & Mesh Viewer Main Application Controller
local config = require("config")
local registry = require("map_registry")
local picker = require("picker")
local viewer = require("viewer")

local app = {
    state = "picker", -- "picker" or "viewer"
    initialized = false,
}

-- Global handles for headless test assertions & drive tapes
YV = {
    app = app,
    config = config,
    registry = registry,
    picker = picker,
    viewer = viewer,
}

local function setup_scene()
    if app.initialized then return end
    config.load_settings()
    picker.init()
    app.initialized = true
end

-- ── 3D Render Hook (called inside BeginMode3D/EndMode3D) ───────────────────
function lp_draw3d()
    setup_scene()
    if app.state == "viewer" then
        viewer.render_3d()
    end
end

-- ── 2D Render Hook ──────────────────────────────────────────────────────────
function lp_draw2d()
    -- 2D screen overlays if needed
end

-- ── ImGui Frame Hook (called inside rlImGuiBeginDelta/End) ───────────────────
function lp_frame()
    setup_scene()
    local dt = lp.rl.get_frame_time()

    if app.state == "picker" then
        local stage_to_open = picker.frame(dt)
        if stage_to_open then
            viewer.open_stage(stage_to_open)
            app.state = "viewer"
        end
    elseif app.state == "viewer" then
        local next_state = viewer.frame(dt)
        if next_state == "picker" then
            app.state = "picker"
        end
    end
end
