local M = {}

-- Исходный глобальный текст (синк с ядром/проводником)
_G.notepad_text = _G.notepad_text or "This is a simple text document.\nYou can write text here using your keyboard."

-- Vim-состояние редактора
local lines = nil
local cur_line = 1
local cur_col = 1
local scroll_offset = 0
local mode = "NORMAL" -- NORMAL, INSERT, COMMAND
local command_text = ""
local pending_key = nil
local yanked_line = nil
local last_notepad_text = nil

-- Статусные сообщения внизу экрана
local status_message = nil
local status_timer = 0

-- История для Undo
local undo_stack = {}
local function push_undo(state_lines)
    local snap = {}
    for i, l in ipairs(state_lines) do
        snap[i] = l
    end
    table.insert(undo_stack, snap)
    if #undo_stack > 50 then
        table.remove(undo_stack, 1) -- ограничение истории изменений
    end
end

local function pop_undo()
    if #undo_stack > 0 then
        local snap = table.remove(undo_stack)
        local restored = {}
        for i, l in ipairs(snap) do
            restored[i] = l
        end
        return restored
    end
    return nil
end

-- Вспомогательные функции парсинга
local function sync_to_lines()
    local text = _G.notepad_text or ""
    local t_lines = {}
    for line in string.gmatch(text .. "\n", "(.-)\r?\n") do
        table.insert(t_lines, line)
    end
    if #t_lines == 0 then t_lines = {""} end
    return t_lines
end

local function sync_to_text(t_lines)
    _G.notepad_text = table.concat(t_lines, "\n")
end

local function do_save(filename)
    sync_to_text(lines)
    saveFile(filename, _G.notepad_text)
    if type(_G.refresh_explorer) == "function" then
        _G.refresh_explorer()
    end
    status_message = '"' .. filename .. '" written'
    status_timer = getUptime() + 3
end

local last_blink_state = -1

-- Рендеринг интерфейса Vim
M.draw = function(win, mx, my, mdown, dt)
    -- Проверка внешнего изменения файла (например, если открыли через Explorer)
    if _G.notepad_text ~= last_notepad_text then
        lines = sync_to_lines()
        last_notepad_text = _G.notepad_text
        cur_line = 1
        cur_col = 1
        scroll_offset = 0
    end

    -- 1. Шапка окна (Header)
    drawRect(win.x, win.y, win.w, 24, 0x2E303B)
    drawText("EQVIM.LUA - NOTES.TXT", win.x + 8, win.y + 6, 0xE5E9F0)

    -- Кнопка Save для мышекликателей (а-ля гибридный режим)
    if button("SAVE", win.x + win.w - 55, win.y + 3, 50, 18) then
        do_save("NOTES.TXT")
    end

    -- Расчет высоты текстового окна с учетом статус-бара
    local line_h = 14
    local text_y = win.y + 28
    local status_bar_h = 18
    local max_visible_lines = math.floor((win.h - 24 - status_bar_h) / line_h)

    -- Автопрокрутка за курсором
    if cur_line > scroll_offset + max_visible_lines then
        scroll_offset = cur_line - max_visible_lines
    elseif cur_line < scroll_offset + 1 then
        scroll_offset = cur_line - 1
    end

    -- 2. Отрисовка строк текста
    local end_visible = math.min(#lines, scroll_offset + max_visible_lines)
    for idx = scroll_offset + 1, end_visible do
        local line_y = text_y + (idx - scroll_offset - 1) * line_h
        local line_text = lines[idx] or ""

        -- Подсветка текущей строки
        if idx == cur_line then
            drawRect(win.x, line_y, win.w, line_h, 0x343746) -- Мягкий серый фон
        end

        drawText(line_text, win.x + 8, line_y, 0xF8F8F2)
    end

    -- 3. Отрисовка курсора
    local np_blink = math.floor(getUptime() * 2) % 2
    if np_blink ~= last_blink_state then
        _G.needs_redraw = true
        last_blink_state = np_blink
    end

    -- Рисуем курсор только если строка видима на экране
    if cur_line >= scroll_offset + 1 and cur_line <= end_visible then
        local cur_y_screen = text_y + (cur_line - scroll_offset - 1) * line_h
        local cur_x_screen = win.x + 8 + (cur_col - 1) * 8

        if mode == "NORMAL" then
            -- Зеленый блок в NORMAL моде
            drawRect(cur_x_screen, cur_y_screen, 8, 14, 0x50FA7B)
            -- Отрисовываем символ поверх курсора темным цветом
            local current_line_text = lines[cur_line] or ""
            local char_at_cursor = string.sub(current_line_text, cur_col, cur_col)
            if char_at_cursor == "" then char_at_cursor = " " end
            drawText(char_at_cursor, cur_x_screen, cur_y_screen, 0x1E1F29)
        elseif mode == "INSERT" and np_blink == 0 then
            -- Тонкая голубая линия в INSERT моде
            drawRect(cur_x_screen, cur_y_screen, 2, 14, 0x8BE9FD)
        end
    end

    -- 4. Отрисовка статус-бара или командной строки
    local bottom_y = win.y + win.h - status_bar_h
    if mode == "COMMAND" then
        -- Командная строка снизу
        drawRect(win.x, bottom_y, win.w, status_bar_h, 0x1E1F29)
        drawText(":" .. command_text, win.x + 8, bottom_y + 2, 0xF8F8F2)
        -- Курсор командной строки
        local cmd_cursor_x = win.x + 8 + (1 + string.len(command_text)) * 8
        drawRect(cmd_cursor_x, bottom_y + 2, 8, 14, 0x50FA7B)
    elseif status_message and getUptime() < status_timer then
        -- Показываем временное сообщение о сохранении/ошибке
        drawRect(win.x, bottom_y, win.w, status_bar_h, 0x21222C)
        drawText(status_message, win.x + 8, bottom_y + 2, 0x50FA7B)
    else
        -- Классический статус-бар (Powerline-стиль)
        drawRect(win.x, bottom_y, win.w, status_bar_h, 0x21222C)

        -- Режим (NORMAL/INSERT)
        local mode_color = 0x50FA7B
        local mode_text_color = 0x282A36
        if mode == "INSERT" then
            mode_color = 0x8BE9FD
        end
        drawRect(win.x, bottom_y, 65, status_bar_h, mode_color)
        drawText(mode, win.x + 8, bottom_y + 2, mode_text_color)

        -- Имя файла
        drawText("NOTES.TXT", win.x + 75, bottom_y + 2, 0xF8F8F2)

        -- Позиция Ln, Col
        local pos_str = string.format("L:%d/%d C:%d", cur_line, #lines, cur_col)
        local pos_w = string.len(pos_str) * 8
        drawText(pos_str, win.x + win.w - pos_w - 10, bottom_y + 2, 0xF8F8F2)
    end
end

-- Обработка событий клавиатуры
M.handle_key = function(win, key, char)
    if not lines then
        lines = sync_to_lines()
    end

    --------------------------------------------------------
    -- 1. Режим ВСТАВКИ (INSERT)
    --------------------------------------------------------
    if mode == "INSERT" then
        if key == 1 or key == KEY_ESC then -- ESC
            push_undo(lines)
            mode = "NORMAL"
            -- Корректируем курсор, чтобы не стоял за пределами строки в NORMAL
            if cur_col > 1 and cur_col > string.len(lines[cur_line]) then
                cur_col = math.max(1, string.len(lines[cur_line]))
            end

        elseif key == 14 or key == KEY_BACKSPACE then -- BACKSPACE
            if cur_col > 1 then
                local line = lines[cur_line]
                lines[cur_line] = string.sub(line, 1, cur_col - 2) .. string.sub(line, cur_col)
                cur_col = cur_col - 1
            elseif cur_line > 1 then
                -- Объединяем строки
                local prev_line = lines[cur_line - 1]
                local prev_len = string.len(prev_line)
                lines[cur_line - 1] = prev_line .. lines[cur_line]
                table.remove(lines, cur_line)
                cur_line = cur_line - 1
                cur_col = prev_len + 1
            end

        elseif key == 28 or key == KEY_ENTER then -- ENTER
            local line = lines[cur_line]
            local left = string.sub(line, 1, cur_col - 1)
            local right = string.sub(line, cur_col)
            lines[cur_line] = left
            table.insert(lines, cur_line + 1, right)
            cur_line = cur_line + 1
            cur_col = 1

        -- Навигация стрелками внутри режима вставки
        elseif key == KEY_UP or key == 0x148 then
            if cur_line > 1 then
                cur_line = cur_line - 1
                cur_col = math.min(cur_col, string.len(lines[cur_line]) + 1)
            end
        elseif key == KEY_DOWN or key == 0x150 then
            if cur_line < #lines then
                cur_line = cur_line + 1
                cur_col = math.min(cur_col, string.len(lines[cur_line]) + 1)
            end
        elseif key == KEY_LEFT or key == 0x14B then
            cur_col = math.max(1, cur_col - 1)
        elseif key == KEY_RIGHT or key == 0x14D then
            cur_col = math.min(string.len(lines[cur_line]) + 1, cur_col + 1)

        -- Обычные печатные символы
        elseif string.len(char) > 0 and string.byte(char) >= 32 then
            local line = lines[cur_line] or ""
            lines[cur_line] = string.sub(line, 1, cur_col - 1) .. char .. string.sub(line, cur_col)
            cur_col = cur_col + string.len(char)
        end

        sync_to_text(lines)
        last_notepad_text = _G.notepad_text

    --------------------------------------------------------
    -- 2. ОБЫЧНЫЙ режим (NORMAL)
    --------------------------------------------------------
    elseif mode == "NORMAL" then
        -- Навигация (h, j, k, l или стрелки)
        if key == KEY_UP or key == 0x148 or char == "k" then
            if cur_line > 1 then
                cur_line = cur_line - 1
                cur_col = math.min(cur_col, math.max(1, string.len(lines[cur_line])))
            end
        elseif key == KEY_DOWN or key == 0x150 or char == "j" then
            if cur_line < #lines then
                cur_line = cur_line + 1
                cur_col = math.min(cur_col, math.max(1, string.len(lines[cur_line])))
            end
        elseif key == KEY_LEFT or key == 0x14B or char == "h" then
            cur_col = math.max(1, cur_col - 1)
        elseif key == KEY_RIGHT or key == 0x14D or char == "l" then
            cur_col = math.min(math.max(1, string.len(lines[cur_line])), cur_col + 1)

        -- Переходы в режим вставки
        elseif char == "i" then
            push_undo(lines)
            mode = "INSERT"
        elseif char == "a" then
            push_undo(lines)
            mode = "INSERT"
            cur_col = math.min(string.len(lines[cur_line]) + 1, cur_col + 1)
        elseif char == "I" then
            push_undo(lines)
            mode = "INSERT"
            cur_col = 1
        elseif char == "A" then
            push_undo(lines)
            mode = "INSERT"
            cur_col = string.len(lines[cur_line]) + 1

        -- Открытие новых строк
        elseif char == "o" then
            push_undo(lines)
            table.insert(lines, cur_line + 1, "")
            cur_line = cur_line + 1
            cur_col = 1
            mode = "INSERT"
            sync_to_text(lines)
            last_notepad_text = _G.notepad_text
        elseif char == "O" then
            push_undo(lines)
            table.insert(lines, cur_line, "")
            cur_col = 1
            mode = "INSERT"
            sync_to_text(lines)
            last_notepad_text = _G.notepad_text

        -- Удаление символа под курсором (x)
        elseif char == "x" then
            push_undo(lines)
            local line = lines[cur_line]
            if string.len(line) > 0 then
                lines[cur_line] = string.sub(line, 1, cur_col - 1) .. string.sub(line, cur_col + 1)
                cur_col = math.min(cur_col, math.max(1, string.len(lines[cur_line])))
                sync_to_text(lines)
                last_notepad_text = _G.notepad_text
            end

        -- Удаление всей строки (dd)
        elseif char == "d" then
            if pending_key == "d" then
                push_undo(lines)
                yanked_line = lines[cur_line]
                table.remove(lines, cur_line)
                if #lines == 0 then lines = {""} end
                if cur_line > #lines then cur_line = #lines end
                cur_col = math.min(cur_col, math.max(1, string.len(lines[cur_line])))
                sync_to_text(lines)
                last_notepad_text = _G.notepad_text
                pending_key = nil
            else
                pending_key = "d"
            end

        -- Копирование строки (yy)
        elseif char == "y" then
            if pending_key == "y" then
                yanked_line = lines[cur_line]
                status_message = "1 line yanked"
                status_timer = getUptime() + 2
                pending_key = nil
            else
                pending_key = "y"
            end

        -- Вставка скопированной строки (p)
        elseif char == "p" then
            if yanked_line then
                push_undo(lines)
                table.insert(lines, cur_line + 1, yanked_line)
                cur_line = cur_line + 1
                cur_col = 1
                sync_to_text(lines)
                last_notepad_text = _G.notepad_text
            end

        -- Быстрые прыжки по строке
        elseif char == "0" then
            cur_col = 1
        elseif char == "$" then
            cur_col = math.max(1, string.len(lines[cur_line]))

        -- Переход в командную строку (:)
        elseif char == ":" then
            mode = "COMMAND"
            command_text = ""

        -- Многоуровневый ОТКАТ (u)
        elseif char == "u" then
            local prev_lines = pop_undo()
            if prev_lines then
                lines = prev_lines
                if cur_line > #lines then cur_line = #lines end
                cur_col = math.min(cur_col, math.max(1, string.len(lines[cur_line])))
                sync_to_text(lines)
                last_notepad_text = _G.notepad_text
                status_message = "Undo applied!"
                status_timer = getUptime() + 2
            else
                status_message = "Already at oldest change"
                status_timer = getUptime() + 2
            end
        else
            pending_key = nil -- сбрасываем незавершенные комбо
        end

    --------------------------------------------------------
    -- 3. Режим командной строки (COMMAND)
    --------------------------------------------------------
    elseif mode == "COMMAND" then
        if key == 1 or key == KEY_ESC then -- ESC
            mode = "NORMAL"
        elseif key == 14 or key == KEY_BACKSPACE then -- BACKSPACE
            if string.len(command_text) > 0 then
                command_text = string.sub(command_text, 1, -2)
            else
                mode = "NORMAL"
            end
        elseif key == 28 or key == KEY_ENTER then -- ENTER (выполнение)
            if command_text == "w" then
                do_save("NOTES.TXT")
            elseif command_text == "q" then
                -- Очистка буфера (в нашей ОС это аналог закрытия/сброса)
                _G.notepad_text = ""
                lines = {""}
                cur_line = 1
                cur_col = 1
                scroll_offset = 0
                status_message = "Buffer cleared"
                status_timer = getUptime() + 2
            elseif command_text == "wq" then
                do_save("NOTES.TXT")
                _G.notepad_text = ""
                lines = {""}
                cur_line = 1
                cur_col = 1
                scroll_offset = 0
            else
                status_message = "Unknown command: :" .. command_text
                status_timer = getUptime() + 3
            end
            mode = "NORMAL"
        elseif string.len(char) > 0 and string.byte(char) >= 32 then
            command_text = command_text .. char
        end
    end
end

return M