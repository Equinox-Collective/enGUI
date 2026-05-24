-- init.lua
print("Equinox System GUI Loading...")

local wallpaper = "/res/BG.BMP"
local blur_active = true

function on_tick()
    -- 1. Рисуем обои
    drawBitmap(wallpaper, 0, 0)
    
    -- 2. Если открыто меню - блюрим фон
    if menu_open then
        applyBlur(100, 100, 300, 400)
        drawRect(100, 100, 300, 400, 0x88000000) -- Полупрозрачное меню
    end

    -- 3. Рисуем таскбар
    drawRect(0, 736, 1024, 32, 0x1A1A1A)
    drawText("Equinox START", 10, 745, 0xFFFFFF)
end