local stage_loader = require("stage_loader")
local inspector = require("inspector")

local viewer = {
    camera = {
        target = { x = 0, y = 0, z = 0 },
        curr_target = { x = 0, y = 0, z = 0 },
        eye = { x = 0, y = 30, z = 45 },
        yaw = 0.0,
        curr_yaw = 0.0,
        pitch = 0.65, -- ~37 degrees
        curr_pitch = 0.65,
        distance = 50.0,
        curr_distance = 50.0,
        fov = 45.0,
        fly_velocity = { x = 0, y = 0, z = 0 },
        fly_speed = 35.0,
        smooth_speed = 18.0, -- Critically damped spring speed for responsive & silky smooth feel
        is_orbiting = false,
        is_panning = false,
        is_flying = false,
    },
    toggles = {
        textures = true,
        wireframe = false,
        vertex_lighting = true,
        colliders = false,     -- OFF by default as requested
        door_triggers = false, -- OFF by default as requested
        props = true,
        in_game_camera = false,
        show_inspector = true,
        show_grid = true,
        toolbar_collapsed = false,
    },
    saved_free_cam = nil,
    return_to_picker = false,
}

local function clamp(val, min_val, max_val)
    if val < min_val then return min_val end
    if val > max_val then return max_val end
    return val
end

function viewer.open_stage(stage_desc)
    local sc = stage_loader.load_stage(stage_desc)
    viewer.scene = sc
    viewer.return_to_picker = false

    -- Focus camera on stage bounds immediately on open
    viewer.focus_camera(true)

    -- Check if in-game camera pitch is available from SCM
    if sc and sc.scm_camera and sc.scm_camera.valid then
        if viewer.toggles.in_game_camera then
            viewer.apply_in_game_camera(true)
        end
    end
end

function viewer.focus_camera(snap)
    local sc = viewer.scene
    if not sc then return end
    local c = viewer.camera

    c.target = { x = sc.center.x, y = sc.center.y, z = sc.center.z }
    local r = math.max(6.0, sc.radius)
    c.distance = r * 1.3
    c.yaw = 0.0
    c.pitch = 0.65

    viewer.update_camera_eye(0, snap == true)
end

function viewer.apply_in_game_camera(snap)
    local sc = viewer.scene
    if not sc then return end
    local c = viewer.camera

    local pitch = 0.79 -- 45 degrees outdoor default
    if sc.scm_camera and sc.scm_camera.valid then
        pitch = sc.scm_camera.pitch
    end

    c.target = { x = sc.center.x, y = sc.center.y, z = sc.center.z }
    local r = math.max(6.0, sc.radius)
    c.distance = r * 1.25
    c.yaw = 0.0
    c.pitch = pitch
    viewer.update_camera_eye(0, snap == true)
end

function viewer.update_camera_eye(dt, snap)
    local c = viewer.camera
    c.pitch = clamp(c.pitch, -1.55, 1.55)
    c.distance = clamp(c.distance, 2.0, 1000.0)

    if snap or dt == nil or dt <= 0 then
        c.curr_target.x = c.target.x
        c.curr_target.y = c.target.y
        c.curr_target.z = c.target.z
        c.curr_yaw = c.yaw
        c.curr_pitch = c.pitch
        c.curr_distance = c.distance
    else
        local lerp_factor = 1.0 - math.exp(-dt * (c.smooth_speed or 18.0))
        c.curr_target.x = c.curr_target.x + (c.target.x - c.curr_target.x) * lerp_factor
        c.curr_target.y = c.curr_target.y + (c.target.y - c.curr_target.y) * lerp_factor
        c.curr_target.z = c.curr_target.z + (c.target.z - c.curr_target.z) * lerp_factor
        c.curr_yaw = c.curr_yaw + (c.yaw - c.curr_yaw) * lerp_factor
        c.curr_pitch = c.curr_pitch + (c.pitch - c.curr_pitch) * lerp_factor
        c.curr_distance = c.curr_distance + (c.distance - c.curr_distance) * lerp_factor
    end

    local cos_p = math.cos(c.curr_pitch)
    local sin_p = math.sin(c.curr_pitch)
    local cos_y = math.cos(c.curr_yaw)
    local sin_y = math.sin(c.curr_yaw)

    c.eye.x = c.curr_target.x + c.curr_distance * cos_p * sin_y
    c.eye.y = c.curr_target.y + c.curr_distance * sin_p
    c.eye.z = c.curr_target.z + c.curr_distance * cos_p * cos_y

    lp.rl.set_camera(c.eye.x, c.eye.y, c.eye.z, c.curr_target.x, c.curr_target.y, c.curr_target.z, c.fov)
end

function viewer.handle_input(dt)
    local io = ig.get_io()
    local c = viewer.camera

    -- 1. Hotkeys (when not typing in text inputs)
    if not io.want_capture_keyboard then
        -- Escape or Back button -> return to picker
        if lp.rl.is_key_pressed(lp.rl.key.Escape) or (ig.key and ig.is_key_pressed(ig.key.Escape)) then
            viewer.return_to_picker = true
        end

        -- 'F' -> Focus camera on map
        if lp.rl.is_key_pressed(lp.rl.key.F) or (ig.key and ig.is_key_pressed(ig.key.F)) then
            viewer.focus_camera()
        end

        -- 'T' -> Toggle Textures
        if lp.rl.is_key_pressed(lp.rl.key.T) then
            viewer.toggles.textures = not viewer.toggles.textures
        end
        -- 'W' -> Toggle Wireframe
        if lp.rl.is_key_pressed(lp.rl.key.W) and not lp.rl.is_mouse_button_down(1) then
            viewer.toggles.wireframe = not viewer.toggles.wireframe
        end

        -- 'L' -> Toggle Vertex Lighting
        if lp.rl.is_key_pressed(lp.rl.key.L) then
            viewer.toggles.vertex_lighting = not viewer.toggles.vertex_lighting
        end

        -- 'C' -> Toggle Colliders
        if lp.rl.is_key_pressed(lp.rl.key.C) then
            viewer.toggles.colliders = not viewer.toggles.colliders
        end

        -- 'D' -> Toggle Door Triggers
        if lp.rl.is_key_pressed(lp.rl.key.D) and not lp.rl.is_mouse_button_down(1) then
            viewer.toggles.door_triggers = not viewer.toggles.door_triggers
        end
    end

    -- 2. Mouse & Navigation (Godot 3D UX standard)
    if not io.want_capture_mouse then
        local m_delta_x, m_delta_y = lp.rl.get_mouse_delta()
        local wheel = lp.rl.get_mouse_wheel()

        -- Wheel: Cursor-anchored Zoom / Dolly
        if wheel ~= 0 then
            local zoom_factor = 1.15 ^ (-wheel)
            c.distance = clamp(c.distance * zoom_factor, 2.0, 1000.0)
            if viewer.toggles.in_game_camera then
                viewer.toggles.in_game_camera = false
            end
        end

        local shift_held = lp.rl.is_key_down(lp.rl.key.LeftShift) or lp.rl.is_key_down(lp.rl.key.RightShift)
        local mmb_down = lp.rl.is_mouse_button_down(2) -- Middle button
        local rmb_down = lp.rl.is_mouse_button_down(1) -- Right button

        -- Middle-drag Orbit / Tilt vs Shift+Middle-drag Pan
        if mmb_down then
            if shift_held then
                -- Shift + Middle: 3D Pan in view plane
                local cos_y = math.cos(c.yaw)
                local sin_y = math.sin(c.yaw)
                local right_x = cos_y
                local right_z = -sin_y

                local pan_speed = c.distance * 0.0015
                c.target.x = c.target.x - right_x * m_delta_x * pan_speed
                c.target.z = c.target.z - right_z * m_delta_x * pan_speed
                c.target.y = c.target.y + m_delta_y * pan_speed

                if viewer.toggles.in_game_camera then
                    viewer.toggles.in_game_camera = false
                end
            else
                -- Middle: Orbit / Tilt around view center
                c.yaw = c.yaw - m_delta_x * 0.004
                c.pitch = clamp(c.pitch + m_delta_y * 0.004, -1.55, 1.55)
                if viewer.toggles.in_game_camera then
                    viewer.toggles.in_game_camera = false
                end
            end
        end

        -- Right-drag Hold: FPS Fly (mouse look + WASD/QE)
        if rmb_down then
            lp.rl.set_mouse_cursor(lp.rl.CURSOR_CROSSHAIR)
            c.yaw = c.yaw - m_delta_x * 0.0035
            c.pitch = clamp(c.pitch + m_delta_y * 0.0035, -1.55, 1.55)

            local cos_p = math.cos(c.pitch)
            local sin_p = math.sin(c.pitch)
            local cos_y = math.cos(c.yaw)
            local sin_y = math.sin(c.yaw)

            local fwd_x = -sin_y * cos_p
            local fwd_y = -sin_p
            local fwd_z = -cos_y * cos_p

            local right_x = cos_y
            local right_z = -sin_y

            local max_speed = c.fly_speed * (shift_held and 3.0 or 1.0)
            local target_vx, target_vy, target_vz = 0, 0, 0
            if lp.rl.is_key_down(lp.rl.key.W) then
                target_vx = target_vx + fwd_x * max_speed
                target_vy = target_vy + fwd_y * max_speed
                target_vz = target_vz + fwd_z * max_speed
            end
            if lp.rl.is_key_down(lp.rl.key.S) then
                target_vx = target_vx - fwd_x * max_speed
                target_vy = target_vy - fwd_y * max_speed
                target_vz = target_vz - fwd_z * max_speed
            end
            if lp.rl.is_key_down(lp.rl.key.D) then
                target_vx = target_vx + right_x * max_speed
                target_vz = target_vz + right_z * max_speed
            end
            if lp.rl.is_key_down(lp.rl.key.A) then
                target_vx = target_vx - right_x * max_speed
                target_vz = target_vz - right_z * max_speed
            end
            if lp.rl.is_key_down(lp.rl.key.E) or lp.rl.is_key_down(lp.rl.key.Space) then
                target_vy = target_vy + max_speed
            end
            if lp.rl.is_key_down(lp.rl.key.Q) or lp.rl.is_key_down(lp.rl.key.LeftCtrl) then
                target_vy = target_vy - max_speed
            end

            local accel = 1.0 - math.exp(-dt * 15.0)
            c.fly_velocity.x = c.fly_velocity.x + (target_vx - c.fly_velocity.x) * accel
            c.fly_velocity.y = c.fly_velocity.y + (target_vy - c.fly_velocity.y) * accel
            c.fly_velocity.z = c.fly_velocity.z + (target_vz - c.fly_velocity.z) * accel

            c.target.x = c.target.x + c.fly_velocity.x * dt
            c.target.y = c.target.y + c.fly_velocity.y * dt
            c.target.z = c.target.z + c.fly_velocity.z * dt

            if viewer.toggles.in_game_camera then
                viewer.toggles.in_game_camera = false
            end
        else
            lp.rl.set_mouse_cursor(lp.rl.CURSOR_DEFAULT)
            c.fly_velocity.x = 0
            c.fly_velocity.y = 0
            c.fly_velocity.z = 0
        end

        viewer.update_camera_eye(dt, false)
    end
end

function viewer.render_floating_toolbar(screen_w)
    local padding = 6.0
    local btn_h = 28.0
    local pill_x = 12.0
    local pill_y = 12.0

    -- Collapsed state: compact pill with 'V' button to bring it back
    if viewer.toggles.toolbar_collapsed then
        local collapsed_w = 40.0
        local collapsed_h = btn_h + padding * 2.0

        ig.set_next_window_pos(pill_x, pill_y, ig.Cond_Always)
        ig.set_next_window_size(collapsed_w, collapsed_h, ig.Cond_Always)
        ig.set_next_window_bg_alpha(0.92)
        local flags = (ig.wflag and (ig.wflag.NoDecoration | ig.wflag.NoMove | ig.wflag.NoSavedSettings)) or 0
        ig.window("##viewer_floating_toolbar_collapsed", flags, function()
            if ig.button("V##expand_toolbar", 28, btn_h) then
                viewer.toggles.toolbar_collapsed = false
            end
            if ig.is_item_hovered() and ig.tooltip_ then
                ig.tooltip_(function()
                    ig.text("Expand Toolbar (V)")
                end)
            end
        end)
        return
    end

    -- Expanded state: 2-line floating pill toolbar
    local inspector_w = (viewer.toggles.show_inspector and (inspector.sidebar_w or 320)) or 0
    local max_avail = screen_w - inspector_w - 24.0
    local toolbar_w = math.max(300.0, math.min(480.0, max_avail))
    local row_spacing = 4.0
    local toolbar_h = btn_h * 2.0 + row_spacing + padding * 2.0

    ig.set_next_window_pos(pill_x, pill_y, ig.Cond_Always)
    ig.set_next_window_size(toolbar_w, toolbar_h, ig.Cond_Always)
    ig.set_next_window_bg_alpha(0.92)
    local flags = (ig.wflag and (ig.wflag.NoDecoration | ig.wflag.NoMove | ig.wflag.NoSavedSettings)) or 0
    ig.window("##viewer_floating_toolbar", flags, function()
        -- ── Line 1: Navigation & View Controls ──────────────────────────
        -- 1. Back to maps button
        if ig.button("< Maps (Esc)", 100, btn_h) then
            viewer.return_to_picker = true
        end

        ig.same_line(0, 8)
        ig.text_colored("|", 0.35, 0.35, 0.4, 1.0)
        ig.same_line(0, 8)

        -- 2. Frame (F)
        if ig.button("Frame (F)", 76, btn_h) then
            viewer.focus_camera()
        end

        ig.same_line(0, 4)

        -- 3. In-Game Cam
        local cam_on = viewer.toggles.in_game_camera
        if cam_on then
            ig.push_style_color(ig.Col_Button, 0.2, 0.7, 0.95, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("In-Game Cam", 102, btn_h) then
            viewer.toggles.in_game_camera = not viewer.toggles.in_game_camera
            if viewer.toggles.in_game_camera then
                viewer.apply_in_game_camera()
            end
        end
        if cam_on then ig.pop_style_color(2) end

        ig.same_line(0, 4)

        -- 4. Inspector
        local insp_on = viewer.toggles.show_inspector
        if insp_on then
            ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("Inspector", 84, btn_h) then
            viewer.toggles.show_inspector = not viewer.toggles.show_inspector
        end
        if insp_on then ig.pop_style_color(2) end

        ig.same_line(0, 8)
        ig.text_colored("|", 0.35, 0.35, 0.4, 1.0)
        ig.same_line(0, 8)

        -- 5. Collapse Button (^)
        if ig.button("^##collapse_toolbar", 28, btn_h) then
            viewer.toggles.toolbar_collapsed = true
        end
        if ig.is_item_hovered() and ig.tooltip_ then
            ig.tooltip_(function()
                ig.text("Collapse Toolbar (^)")
            end)
        end

        -- ── Line 2: Render & Shading Toggles ──────────────────────────
        -- 1. Textures Toggle (T)
        local tex_on = viewer.toggles.textures
        if tex_on then
            ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("Textures (T)", 96, btn_h) then
            viewer.toggles.textures = not viewer.toggles.textures
        end
        if tex_on then ig.pop_style_color(2) end

        ig.same_line(0, 4)

        -- 2. Wireframe Toggle (W)
        local wire_on = viewer.toggles.wireframe
        if wire_on then
            ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("Wire (W)", 72, btn_h) then
            viewer.toggles.wireframe = not viewer.toggles.wireframe
        end
        if wire_on then ig.pop_style_color(2) end

        ig.same_line(0, 4)

        -- 3. Vertex Lighting Toggle (L)
        local light_on = viewer.toggles.vertex_lighting
        if light_on then
            ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("Lighting (L)", 88, btn_h) then
            viewer.toggles.vertex_lighting = not viewer.toggles.vertex_lighting
        end
        if light_on then ig.pop_style_color(2) end

        ig.same_line(0, 4)

        -- 4. Colliders Toggle (C)
        local coll_on = viewer.toggles.colliders
        if coll_on then
            ig.push_style_color(ig.Col_Button, 0.1, 0.85, 0.4, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("Colliders (C)", 96, btn_h) then
            viewer.toggles.colliders = not viewer.toggles.colliders
        end
        if coll_on then ig.pop_style_color(2) end

        ig.same_line(0, 4)

        -- 5. Door Triggers Toggle (D)
        local door_on = viewer.toggles.door_triggers
        if door_on then
            ig.push_style_color(ig.Col_Button, 0.96, 0.75, 0.1, 1.0)
            ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
        end
        if ig.button("Doors (D)", 80, btn_h) then
            viewer.toggles.door_triggers = not viewer.toggles.door_triggers
        end
        if door_on then ig.pop_style_color(2) end
    end)
end

function viewer.render_3d()
    local sc = viewer.scene
    if not sc then return end

    if viewer.toggles.show_grid then
        local gy = sc.bounds.min.y - 0.2
        local grid_alpha = 255
        lp.rl.draw_grid(30, math.max(2.0, sc.radius / 15.0), sc.center.x, gy, sc.center.z, grid_alpha)
    end
    stage_loader.render_scene(sc, {
        textures = viewer.toggles.textures,
        wireframe = viewer.toggles.wireframe,
        vertex_lighting = viewer.toggles.vertex_lighting,
        colliders = viewer.toggles.colliders,
        door_triggers = viewer.toggles.door_triggers,
        props = viewer.toggles.props,
    })
end

function viewer.frame(dt)
    viewer.handle_input(dt)

    local screen_w, screen_h = lp.rl.get_screen_size()
    viewer.render_floating_toolbar(screen_w)

    if viewer.toggles.show_inspector and viewer.scene then
        inspector.render(viewer.scene, screen_w, screen_h)
    end

    if viewer.return_to_picker then
        stage_loader.unload_current_stage()
        viewer.scene = nil
        return "picker"
    end

    return "viewer"
end

return viewer
