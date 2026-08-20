-- drive_picker.lua - Capture gallery picker
local D = require("drive")

print("[drive] Capturing gallery picker...")
D.at(2, function()
    YV.picker.show_settings = false
end)
