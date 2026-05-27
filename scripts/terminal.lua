-- res/sysgui/terminal.lua
local M = {}

local term_lines = {
    "Equinox OS Ring 3 Terminal [Version 2.1]",
    "Welcome to the modular CLI shell.",
    "Press keys to see their raw scancodes in the prompt below!",
    ""
}

_G.term_lines = term_lines
_G.term_input = ""
_G.last_key_debug = 0 -- Глобальная переменная отладчика кодов клавиш

local matrix_mode = false
local matrix_tick = 0
local last_blink_state = -1

local cmd_history = {}
local history_idx = 0
local scroll_offset = 0

local function strip_ansi(s)
    if not s then return "" end
    return (s:gsub("\27%[[%d;]*m", ""))
end

local function term_append_multiline(text)
    text = strip_ansi(text or "")
    if text == "" then return end
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        table.insert(term_lines, line)
    end
end

local function process_command(raw)
    raw = raw or ""
    local s = string.match(raw, "^%s*(.-)%s*$") or ""
    if s == "" then return end

    local verb = string.match(s, "^(%S+)")

    local GUI_LOCAL_COMMANDS = {
        clear   = function() 
            _G.term_lines = {}
            term_lines = _G.term_lines
        end,
        matrix  = function()
            matrix_mode = not matrix_mode
            table.insert(term_lines, "Matrix digital rain: " .. (matrix_mode and "ENABLED" or "DISABLED"))
        end,
        doom    = function()
            table.insert(term_lines, "Launching doom.elf...")
            exec("bin/doom.elf -iwad res/doom1.wad")
        end,
        snake   = function()
            table.insert(term_lines, "Launching snake.elf...")
            exec("bin/snake.elf")
        end,
    }

    local local_handler = GUI_LOCAL_COMMANDS[verb]
    if local_handler then
        local_handler()
        return
    end

    if type(shellExec) ~= "function" then
        table.insert(term_lines, "shellExec() not available")
        return
    end

    local out = shellExec(s)
    term_append_multiline(out)
end

-- Отрисовка
M.draw = function(win, mx, my, mdown, dt)
    if matrix_mode then
        matrix_tick = matrix_tick + 1
        _G.needs_redraw = true
        drawRect(win.x, win.y, win.w, win.h, 0x000000)
        for i = 1, 30 do
            local rx = win.x + ((i * 17) % win.w)
            local speed = 2 + (i % 4)
            local ry = win.y + math.floor((matrix_tick * speed + i * 43) % win.h)
            local char_code = 33 + ((matrix_tick + i) % 90)
            local char_str = string.char(char_code)
            local col = (i % 5 == 0) and 0xFFFFFF or 0x00FF00
            drawText(char_str, rx, ry, col)
        end
        return
    end

    local line_h = 14
    local max_lines = math.floor((win.h - 30) / line_h)
    
    local total_lines = #term_lines
    local end_idx = total_lines - scroll_offset
    local start_idx = end_idx - max_lines + 1
    if start_idx < 1 then start_idx = 1 end

    local draw_y = win.y + 8
    for i = start_idx, end_idx do
        if term_lines[i] then
            drawText(term_lines[i], win.x + 8, draw_y, 0x50FA7B)
            draw_y = draw_y + line_h
        end
    end

    -- Отрисовка строки ввода
    local prompt_y = win.y + win.h - 22
    drawRect(win.x, prompt_y, win.w, 1, 0x2E3440)
    
    -- Динамически формируем префикс с отладочной информацией
    local input_prefix = ">> "
    if scroll_offset > 0 then
        input_prefix = string.format("[%d] >> ", scroll_offset)
    end
    
    -- Добавляем отображение кода последней нажатой клавиши (Сниффер)
    if _G.last_key_debug and _G.last_key_debug > 0 then
        input_prefix = string.format("Code:%d %s", _G.last_key_debug, input_prefix)
    end
    
    drawText(input_prefix .. _G.term_input, win.x + 8, prompt_y + 4, 0xF8F8F2)

    -- Курсор
    local blink = math.floor(getUptime() * 2) % 2
    if blink ~= last_blink_state then
        _G.needs_redraw = true
        last_blink_state = blink
    end
    if blink == 0 then
        local cur_cx = win.x + 8 + (string.len(input_prefix) + string.len(_G.term_input)) * 8
        drawRect(cur_cx, prompt_y + 16, 8, 2, 0x8BE9FD)
    end
end

-- Обработка клавиш приложения
M.handle_key = function(win, key, char)
    -- Сохраняем код нажатой клавиши для вывода на экран
    _G.last_key_debug = key

    if key == 28 then -- ENTER
        if _G.term_input ~= "" then
            table.insert(cmd_history, _G.term_input)
            history_idx = #cmd_history + 1
        end
        table.insert(term_lines, ">> " .. _G.term_input)
        process_command(_G.term_input)
        _G.term_input = ""
        scroll_offset = 0
        
    elseif key == 14 then -- BACKSPACE
        _G.term_input = string.sub(_G.term_input, 1, -2)
        
    elseif key == 15 then -- TAB (100% Безопасный автокомплит)
        local word = string.match(_G.term_input, "(%S+)$") or ""
        if word ~= "" then
            local files = getFiles()
            if type(files) == "table" then
                for _, f in ipairs(files) do
                    -- Проверяем, что файл и его имя корректны
                    if f and type(f.name) == "string" and f.name ~= "" then
                        if string.sub(f.name, 1, string.len(word)) == word then
                            local suffix = string.sub(f.name, string.len(word) + 1)
                            _G.term_input = _G.term_input .. suffix
                            break
                        end
                    end
                end
            end
        end

    elseif key == 72 then -- СТРЕЛКА ВВЕРХ (Предыдущая команда из истории)
        if #cmd_history > 0 then
            history_idx = history_idx - 1
            if history_idx < 1 then history_idx = 1 end
            _G.term_input = cmd_history[history_idx] or ""
        end

    elseif key == 80 then -- СТРЕЛКА ВНИЗ (Следующая команда из истории)
        if #cmd_history > 0 then
            history_idx = history_idx + 1
            if history_idx > #cmd_history then
                history_idx = #cmd_history + 1
                _G.term_input = ""
            else
                _G.term_input = cmd_history[history_idx] or ""
            end
        end

    elseif key == 73 then -- PAGE UP (Скролл вывода назад)
        scroll_offset = scroll_offset + 3
        local max_lines = math.floor((win.h - 30) / 14)
        if scroll_offset > #term_lines - max_lines then
            scroll_offset = #term_lines - max_lines
        end
        if scroll_offset < 0 then scroll_offset = 0 end

    elseif key == 81 then -- PAGE DOWN (Скролл вывода вперед)
        scroll_offset = scroll_offset - 3
        if scroll_offset < 0 then scroll_offset = 0 end

    elseif string.len(char) > 0 and string.byte(char) >= 32 then
        _G.term_input = _G.term_input .. char
    end
end

return M