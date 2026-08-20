-- editor/lua/drive.lua — Headless input tape driver for Ys Map & Mesh Viewer
local D = {
    plan = {},
    f = 0,
    Key = lp.rl.key,
}

function D.at(f, fn)
    local list = D.plan[f] or {}
    list[#list + 1] = fn
    D.plan[f] = list
end

function drive_step()
    D.f = D.f + 1
    local fns = D.plan[D.f]
    if fns then
        for i = 1, #fns do
            fns[i]()
        end
    end
end

function drive_frame()
    -- frame boundary handled in C++
end

function drive_begin()
    lp.drive.active(true)
end

function D.mouse(x, y)      lp.drive.mouse(x, y) end
function D.button(btn, d)   lp.drive.button(btn, d) end
function D.wheel(dy)        lp.drive.wheel(dy) end
function D.key(code, d)     lp.drive.key(code, d) end

function D.click(f, x, y, btn)
    btn = btn or 0
    D.at(f,     function() D.mouse(x, y) end)
    D.at(f + 1, function() D.button(btn, true) end)
    D.at(f + 2, function() D.button(btn, false) end)
end

function D.rclick(f, x, y)
    D.click(f, x, y, 1)
end

function D.drag(f, x0, y0, x1, y1, steps, btn)
    steps = steps or 4
    btn = btn or 0
    D.at(f, function() D.mouse(x0, y0) end)
    D.at(f + 1, function() D.button(btn, true) end)
    for i = 1, steps do
        local t = i / steps
        local x = x0 + (x1 - x0) * t
        local y = y0 + (y1 - y0) * t
        D.at(f + 1 + i, function() D.mouse(x, y) end)
    end
    D.at(f + 2 + steps, function() D.button(btn, false) end)
end

function D.tap(f, code)
    D.at(f,     function() D.key(code, true) end)
    D.at(f + 1, function() D.key(code, false) end)
end

function D.chord(f, mod_code, code)
    D.at(f,     function() D.key(mod_code, true) end)
    D.at(f + 1, function() D.key(code, true) end)
    D.at(f + 2, function() D.key(code, false) end)
    D.at(f + 3, function() D.key(mod_code, false) end)
end

-- Auto-enable drive when required in a drive script
drive_begin()

return D
