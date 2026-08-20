-- drive_picker.lua - Capture gallery picker
local D = require("drive")

print("[drive] Capturing gallery picker with dynamic tab/page texture streaming...")
D.at(2, function()
    YV.picker.show_settings = false
end)

-- Frame 15: Switch tab to Ys Origin
D.at(15, function()
    print("[drive] Switching tab to Ys Origin")
    YV.registry.selected_game = "origin"
    YV.registry.apply_filter()
    YV.picker.current_page = 1
    if lp.async then lp.async.clear_pending() end
end)

-- Frame 35: Switch tab to Ys VI
D.at(35, function()
    print("[drive] Switching tab to Ys VI")
    YV.registry.selected_game = "ys6"
    YV.registry.apply_filter()
    YV.picker.current_page = 1
    if lp.async then lp.async.clear_pending() end
end)

-- Frame 50: Switch to Page 2 of Ys VI
D.at(50, function()
    print("[drive] Navigating to Page 2 of Ys VI")
    YV.picker.current_page = 2
    if lp.async then lp.async.clear_pending() end
end)

-- Frame 70: Switch back to All Games tab
D.at(70, function()
    print("[drive] Switching back to All Games tab")
    YV.registry.selected_game = "all"
    YV.registry.apply_filter()
    YV.picker.current_page = 1
    if lp.async then lp.async.clear_pending() end
end)
