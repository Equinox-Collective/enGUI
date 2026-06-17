local Window = {}
Window.__index = Window

function Window.new(title, x, y, w, h, draw_cb, key_cb)
    local self = setmetatable({}, Window)
    self.title = title
    self.x = x
    self.y = y
    self.w = w
    self.h = h
    self.old_x = x
    self.old_y = y
    self.old_w = w
    self.old_h = h
    self.active = true
    self.borderless = false
    self.fullscreen = false
    self.is_app_container = false
    self.minimized = false
    self.snapped = false -- false, "left", "right"

    if type(draw_cb) == "table" then
        self.draw_cb = draw_cb.draw
        self.key_cb = draw_cb.handle_key
    else
        self.draw_cb = draw_cb
        self.key_cb = key_cb
    end

    if type(animCreate) == "function" then
        self.anim_scale = animCreate(180, 3) -- Smooth EID_EASE_OUT_CUBIC
        animTo(self.anim_scale, 1.0)
    end

    return self
end

function Window:draw(mx, my, mdown, dt)
    if not self.active or self.minimized then return end

    local sw, sh = getScreenSize()
    local scale = 1.0
    if self.anim_scale then
        animStep(self.anim_scale, dt)
        scale = animEval(self.anim_scale)
    end

    local cx = self.x + self.w / 2
    local cy = self.y + self.h / 2
    local win_w = math.floor(self.w * scale)
    local win_h = math.floor(self.h * scale)
    local win_x = math.floor(cx - win_w / 2)
    local win_y = math.floor(cy - win_h / 2)

    if self.fullscreen then
        win_x, win_y = 0, 24
        win_w, win_h = sw, sh - 56
    elseif self.snapped == "left" then
        win_x, win_y = 0, 24
        win_w, win_h = math.floor(sw / 2), sh - 56
    elseif self.snapped == "right" then
        win_x, win_y = math.floor(sw / 2), 24
        win_w, win_h = math.floor(sw / 2), sh - 56
    end

    local active = (focused_window == self)
    
    if not self.borderless then
        local ty = win_y - 28
        
        -- 1. Высококачественная мягкая многослойная тень (Liquid Glass Shadows)
        if type(drawTransparentRect) == "function" then
            drawTransparentRect(win_x - 6, ty - 6, win_w + 12, win_h + 38, 0x000000, 0.12)
            drawTransparentRect(win_x - 3, ty - 3, win_w + 6, win_h + 32, 0x000000, 0.22)
        else
            drawRect(win_x - 2, ty - 2, win_w + 4, win_h + 32, 0x0E0F12)
        end

        -- 2. Светящаяся стеклянная окантовка (Glow Outline)
        local outline_color = active and 0x51AFEF or 0x4B5263
        local outline_opacity = active and 0.55 or 0.25
        if type(drawTransparentRect) == "function" then
            drawTransparentRect(win_x - 1, ty - 1, win_w + 2, win_h + 30, outline_color, outline_opacity)
        else
            drawRect(win_x - 1, ty - 1, win_w + 2, win_h + 30, outline_color)
        end

        -- 3. Акриловое размытие заголовка (Frosted Titlebar)
        if type(drawBlur) == "function" then
            drawBlur(win_x, ty, win_w, 28, active and 0.45 or 0.30)
            -- Накладываем стеклянный глянец
            if type(drawTransparentRect) == "function" then
                drawTransparentRect(win_x, ty, win_w, 28, 0x1E222B, 0.40)
                -- Тонкий блик сверху заголовка
                drawTransparentRect(win_x, ty, win_w, 1, 0xFFFFFF, active and 0.25 or 0.10)
            end
        else
            drawRect(win_x, ty, win_w, 28, active and 0x21252B or 0x1E2227)
        end

        -- Тонкая линия-акцент под заголовком
        if type(drawTransparentRect) == "function" then
            drawTransparentRect(win_x, win_y - 1, win_w, 1, active and 0x51AFEF or 0x3E4452, 0.35)
        else
            drawRect(win_x, win_y - 1, win_w, 1, active and 0x0078D7 or 0x3E4452)
        end

        -- Заголовок окна
        drawText(self.title, win_x + 10, ty + 8, active and 0xFFFFFF or 0xABB2BF)

        -- Кнопки управления (Минимизировать, Развернуть, Закрыть)
        local bx = win_x + win_w - 76
        
        -- Стеклянная кнопка Свернуть [ - ]
        if button("_", bx, ty + 4, 20, 20) then
            self.minimized = true
            _G.needs_redraw = true
            return
        end
        
        -- Стеклянная кнопка Развернуть [ O ]
        if button("O", bx + 24, ty + 4, 20, 20) then
            if self.fullscreen or self.snapped then
                self.fullscreen = false
                self.snapped = false
                self.x, self.y, self.w, self.h = self.old_x, self.old_y, self.old_w, self.old_h
            else
                self.old_x, self.old_y, self.old_w, self.old_h = self.x, self.y, self.w, self.h
                self.fullscreen = true
            end
            _G.needs_redraw = true
        end

        -- Стеклянная кнопка Закрыть [ X ] (Подсвечивается красным при наведении)
        local close_hover = (mx >= bx + 48 and mx < bx + 68 and my >= ty + 4 and my < ty + 24)
        if close_hover and type(drawTransparentRect) == "function" then
            drawTransparentRect(bx + 48, ty + 4, 20, 20, 0xE06C75, 0.40)
        end
        
        if button("X", bx + 48, ty + 4, 20, 20) then
            self.active = false
            _G.needs_redraw = true

            if self.is_app_container then
                if type(getTasks) == "function" and type(killTask) == "function" then
                    local tasks = getTasks()
                    for _, t in ipairs(tasks) do
                        if t.pid ~= 1 and t.pid ~= 2 and t.pid ~= 3 then
                            killTask(t.pid)
                        end
                    end
                end
            end
            return
        end
    end

    -- Тело окна (Window Body)
    if not self.is_app_container then
        if not self.borderless then
            -- Глубокое акриловое стекло тела окна
            if type(drawBlur) == "function" then
                drawBlur(win_x, win_y, win_w, win_h, 0.55)
                if type(drawTransparentRect) == "function" then
                    drawTransparentRect(win_x, win_y, win_w, win_h, 0x1A1C24, 0.50)
                end
            else
                drawRect(win_x, win_y, win_w, win_h, 0x1E1E24)
            end
        end
        
        if self.draw_cb then 
            local original_x, original_y = self.x, self.y
            local original_w, original_h = self.w, self.h
            self.x, self.y = win_x, win_y
            self.w, self.h = win_w, win_h

            self.draw_cb(self, mx, my, mdown, dt) 

            self.x, self.y = original_x, original_y
            self.w, self.h = original_w, original_h
        end
    else
        if type(setAppWindowPos) == "function" then
            if focused_window == self and win_w > 0 and win_h > 0 then
                setAppWindowPos(win_x, win_y, win_w, win_h)
            end
        end
    end

    -- Ручка изменения размера окна (Resize Handle)
    if not self.borderless and not self.fullscreen and not self.snapped and scale >= 0.95 then
        local rx, ry = win_x + win_w - 10, win_y + win_h - 10
        if type(drawTransparentRect) == "function" then
            drawTransparentRect(rx, ry, 10, 10, active and 0x51AFEF or 0x4B5263, 0.6)
        else
            drawRect(rx, ry, 10, 10, active and 0x0078D7 or 0x4E5666)
        end
        if mdown and not last_mdown and mx >= rx and mx < rx+10 and my >= ry and my < ry+10 then
            resizing_win = self
        end
    end
end

function Window:handle_key(key, char)
    if self.active and self.key_cb then
        self.key_cb(self, key, char)
    end
end

return Window