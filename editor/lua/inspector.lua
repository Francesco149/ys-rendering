-- inspector.lua — Stage Scene Inspector Sidebar (Props tree, Materials, Collision stats, SCM metadata)
local inspector = {
    sidebar_w = 320,
    min_w = 220,
    max_w = 540,
    resize_active = false,
    selected_tab = "props", -- "props", "colliders", "materials", "camera"
}

local function clamp(val, min_val, max_val)
    if val < min_val then return min_val end
    if val > max_val then return max_val end
    return val
end

function inspector.render(scene, screen_w, screen_h)
    if not scene then return end

    local w = inspector.sidebar_w
    local x = screen_w - w
    local y = 0
    local h = screen_h

    ig.set_next_window_pos(x, y, ig.Cond_Always)
    ig.set_next_window_size(w, h, ig.Cond_Always)
    ig.set_next_window_bg_alpha(0.92)

    local flags = (ig.wflag and (ig.wflag.NoTitleBar | ig.wflag.NoMove | ig.wflag.NoResize | ig.wflag.NoSavedSettings)) or 0
    ig.window("##stage_inspector", flags, function()
        -- In-window splitter resize handle
        ig.child("##splitter_col", 6, 0, 0, function()
            local sx, sy = ig.get_cursor_screen_pos()
            local dl = ig.get_window_draw_list()
            local aw, ah = ig.get_content_region_avail()
            ig.invisible_button("##inspector_splitter", 6, math.max(ah, 1))

            local hovered = ig.is_item_hovered()
            if hovered and ig.is_mouse_clicked(0) then
                inspector.resize_active = true
            end
            if not ig.is_mouse_down(0) then
                inspector.resize_active = false
            end

            if inspector.resize_active then
                local dx, _ = lp.rl.get_mouse_delta()
                inspector.sidebar_w = clamp(inspector.sidebar_w - dx, inspector.min_w, inspector.max_w)
            end

            if hovered or inspector.resize_active then
                lp.rl.set_mouse_cursor(lp.rl.CURSOR_RESIZE_EW)
                ig.dl_add_rect_filled(dl, sx, sy, sx + 6, sy + ah, 0.96, 0.62, 0.04, 0.85)
            else
                lp.rl.set_mouse_cursor(lp.rl.CURSOR_DEFAULT)
            end
        end)

        ig.same_line(0, 4)

        -- Inspector Content Column
        ig.child("##inspector_content", 0, 0, 0, function()
            local st = scene.stage_desc
            -- Header
            local bc = st.badge_color or { 0.96, 0.62, 0.04, 1.0 }
            ig.text_colored("[" .. st.game_name .. "]", bc[1], bc[2], bc[3], bc[4])
            ig.same_line()
            ig.text_colored(st.stage_id, 0.9, 0.9, 0.95, 1.0)

            ig.text_wrapped(st.title or "Stage")
            ig.text_colored(st.zone or "Zone", 0.5, 0.55, 0.65, 1.0)

            ig.separator()
            ig.spacing()

            -- Stats Overview Card
            ig.text_colored("Statistics", 0.96, 0.62, 0.04, 1.0)
            local tris = scene.total_triangles or 0
            local verts = scene.total_vertices or 0
            local props_count = #(scene.placed_props or {})
            local doors_count = #(scene.door_triggers or {})

            ig.text(string.format("Total Triangles: %d", tris))
            ig.text(string.format("Total Vertices:  %d", verts))
            ig.text(string.format("Placed Props:    %d", props_count))
            ig.text(string.format("Door Triggers:   %d", doors_count))

            ig.spacing()
            ig.separator()

            -- Inspector Tabs
            local tabs = {
                { id = "props", label = "Props (" .. props_count .. ")" },
                { id = "doors", label = "Doors (" .. doors_count .. ")" },
                { id = "colliders", label = "Colliders" },
                { id = "camera", label = "Camera" },
            }

            for i, tab in ipairs(tabs) do
                if i > 1 then ig.same_line(0, 4) end
                local is_sel = (inspector.selected_tab == tab.id)
                if is_sel then
                    ig.push_style_color(ig.Col_Button, 0.96, 0.62, 0.04, 1.0)
                    ig.push_style_color(ig.Col_Text, 0.1, 0.1, 0.12, 1.0)
                end
                if ig.button(tab.label .. "##itab_" .. tab.id, 0, 22) then
                    inspector.selected_tab = tab.id
                end
                if is_sel then ig.pop_style_color(2) end
            end

            ig.spacing()
            ig.separator()

            -- Tab Content
            if inspector.selected_tab == "props" then
                ig.text_colored("Placed Scene Objects (SOB):", 0.7, 0.75, 0.85, 1.0)
                if #scene.placed_props == 0 then
                    ig.text_colored("No placed props in this stage.", 0.5, 0.5, 0.55, 1.0)
                else
                    for i, prop in ipairs(scene.placed_props) do
                        ig.push_id(i)
                        local vis_changed, new_vis = ig.checkbox("##vis", prop.visible)
                        if vis_changed then prop.visible = new_vis end
                        ig.same_line()

                        local label = string.format("%d. %s", i, prop.name or prop.filename or "Prop")
                        ig.selectable(label .. "##sel", false, 0, 0, 18)

                        -- Hover tooltip with transform
                        if ig.is_item_hovered() then
                            ig.tooltip_(function()
                                ig.text("Model: " .. (prop.model_path or ""))
                                ig.text(string.format("Pos:   (%.1f, %.1f, %.1f)", prop.position.x, prop.position.y, prop.position.z))
                                ig.text(string.format("Rot:   (%.1f, %.1f, %.1f) rad", prop.rotation.x, prop.rotation.y, prop.rotation.z))
                                ig.text(string.format("Scale: (%.1f, %.1f, %.1f)", prop.scale.x, prop.scale.y, prop.scale.z))
                            end)
                        end
                        ig.pop_id()
                    end
                end

            elseif inspector.selected_tab == "doors" then
                ig.text_colored("Door & Trigger Meshes:", 0.96, 0.75, 0.1, 1.0)
                if #scene.door_triggers == 0 then
                    ig.text_colored("No door triggers identified in this stage.", 0.5, 0.5, 0.55, 1.0)
                else
                    for i, door in ipairs(scene.door_triggers) do
                        ig.push_id(i)
                        local vis_changed, new_vis = ig.checkbox("##dvis", door.visible)
                        if vis_changed then door.visible = new_vis end
                        ig.same_line()

                        local label = string.format("%d. %s (Trigger)", i, door.name or "Door")
                        ig.selectable(label .. "##dsel", false, 0, 0, 18)

                        if ig.is_item_hovered() then
                            ig.tooltip_(function()
                                ig.text("Trigger Model: " .. (door.model_path or ""))
                                ig.text(string.format("Pos: (%.1f, %.1f, %.1f)", door.position.x, door.position.y, door.position.z))
                            end)
                        end
                        ig.pop_id()
                    end
                end

            elseif inspector.selected_tab == "colliders" then
                ig.text_colored("Collision Geometry (Layers & Meshes):", 0.96, 0.62, 0.04, 1.0)
                ig.spacing()

                -- Walkable Ground (__s)
                if scene.coll_walkable then
                    local vis = (scene.coll_walkable_visible ~= false)
                    local ch, new_vis = ig.checkbox("##vis_walk", vis)
                    if ch then scene.coll_walkable_visible = new_vis end
                    ig.same_line()
                    local w_tris = scene.coll_walkable.info.total_triangles or 0
                    ig.text_colored("[Walkable Floor __s]", 0.1, 0.9, 0.4, 1.0)
                    ig.same_line()
                    ig.text(string.format("(%d tris)", w_tris))
                elseif not scene.coll_origin_mesh then
                    ig.text_colored("[Walkable Floor __s] Not present", 0.45, 0.45, 0.5, 1.0)
                end

                -- Wall Obstacles (__w)
                if scene.coll_wall then
                    local vis = (scene.coll_wall_visible ~= false)
                    local ch, new_vis = ig.checkbox("##vis_wall", vis)
                    if ch then scene.coll_wall_visible = new_vis end
                    ig.same_line()
                    local wl_tris = scene.coll_wall.info.total_triangles or 0
                    ig.text_colored("[Obstacle Walls __w]", 1.0, 0.4, 0.1, 1.0)
                    ig.same_line()
                    ig.text(string.format("(%d tris)", wl_tris))
                elseif not scene.coll_origin_mesh then
                    ig.text_colored("[Obstacle Walls __w] Not present", 0.45, 0.45, 0.5, 1.0)
                end

                -- Camera Boundary (__c)
                if scene.coll_camera then
                    local vis = (scene.coll_camera_visible ~= false)
                    local ch, new_vis = ig.checkbox("##vis_cam", vis)
                    if ch then scene.coll_camera_visible = new_vis end
                    ig.same_line()
                    local c_tris = scene.coll_camera.info.total_triangles or 0
                    ig.text_colored("[Camera Boundary __c]", 0.1, 0.6, 1.0, 1.0)
                    ig.same_line()
                    ig.text(string.format("(%d tris)", c_tris))
                elseif not scene.coll_origin_mesh then
                    ig.text_colored("[Camera Boundary __c] Not present", 0.45, 0.45, 0.5, 1.0)
                end

                -- Origin Collision Mesh (Stage_.YMO)
                if scene.coll_origin_mesh then
                    local vis = (scene.coll_origin_mesh_visible ~= false)
                    local ch, new_vis = ig.checkbox("##vis_origin_coll", vis)
                    if ch then scene.coll_origin_mesh_visible = new_vis end
                    ig.same_line()
                    local o_tris = scene.coll_origin_mesh.info.total_triangles or 0
                    local o_subs = #(scene.coll_origin_mesh.info.submeshes or {})
                    ig.text_colored("[Origin Collider _.ymo]", 0.1, 0.9, 0.4, 1.0)
                    ig.same_line()
                    ig.text(string.format("(%d tris, %d submeshes)", o_tris, o_subs))
                end

                ig.spacing()
                ig.separator()
                ig.spacing()
                ig.text_colored("Layer Roles & Color Coding:", 0.6, 0.65, 0.75, 1.0)
                ig.text_colored("• Emerald (__s):", 0.1, 0.9, 0.4, 1.0)
                ig.same_line()
                ig.text("Walkable floor & terrain collision")
                ig.text_colored("• Orange (__w):", 1.0, 0.4, 0.1, 1.0)
                ig.same_line()
                ig.text("Solid obstacle & wall geometry")
                ig.text_colored("• Sky Blue (__c):", 0.1, 0.6, 1.0, 1.0)
                ig.same_line()
                ig.text("Camera occlusion volume (keeps in-game camera from clipping past walls/tapestries)")
            elseif inspector.selected_tab == "camera" then
                ig.text_colored("Camera & Play Bounds (SCM/SFO):", 0.2, 0.7, 0.95, 1.0)
                if scene.scm_camera and scene.scm_camera.valid then
                    local scm = scene.scm_camera
                    local pitch_deg = scm.pitch * 57.2958
                    ig.text(string.format("Authored Pitch: %.1f deg (%.2f rad)", pitch_deg, scm.pitch))
                    ig.text(string.format("Perspective:    %s", scm.is_topdown and "Indoor Top-Down" or "Outdoor 45 deg"))

                    ig.spacing()
                    ig.text("Camera AABB Bounds:")
                    ig.text(string.format("  Min: (%.1f, %.1f, %.1f)", scm.aabb_min.x, scm.aabb_min.y, scm.aabb_min.z))
                    ig.text(string.format("  Max: (%.1f, %.1f, %.1f)", scm.aabb_max.x, scm.aabb_max.y, scm.aabb_max.z))
                else
                    ig.text_colored("No SCM camera file authored for this stage.", 0.5, 0.5, 0.55, 1.0)
                    ig.text("Default 45° outdoor angle active.")
                end
            end
        end)
    end)
end

return inspector
