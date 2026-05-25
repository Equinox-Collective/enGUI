-- Equinox OS: Desktop Environment
print("enGUI: Desktop Environment starting...")

-- Конфигурация ярлыков приложений
local apps = {
    { name = "Doom Game",   bin = "bin/doom.elf",       color = 0xDE3E3E },
    { name = "Classic Snake", bin = "bin/snake.elf",     color = 0x4CAF50 },
    { name = "Widget Demo", bin = "bin/widget_demo.elf", color = 0x2196F3 },
    { name = "BMP Viewer",  bin = "bin/bmpview.elf",     color = 0xFF9800 },
    { name = "HTML Viewer", bin = "bin/htmlview.elf",    color = 0x9C27B0 },
}

-- Инициализация анимации для выдвижного меню "Пуск" (длительность 200мс, Ease Out Cubic)
local start_menu_anim = animCreate(200.0, 3)
local is_menu_open = false
local menu_anim_target = 0.0

function on_tick(dt)
    -- 1. Шаг анимации меню "Пуск"
    animStep(start_menu_anim, dt)
    local menu_progress = animEval(start_menu_anim)

    -- 2. Очистка экрана (Красивый глубокий градиент обоев рабочего стола)
    drawGradient(0, 0, 1024, 728, 0x141721, 0x0B0C10, true)

    -- 3. Рисуем сетку ярлыков на рабочем столе
    local icon_w, icon_h = 130, 80
    local start_x, start_y = 30, 30
    local spacing_x, spacing_y = 150, 100

    for i, app in ipairs(apps) do
        local col = (i - 1) % 4
        local row = math.floor((i - 1) / 4)
        local x = start_x + col * spacing_x
        local y = start_y + row * spacing_y

        -- Декоративная иконка-прямоугольник
        drawRect(x, y, icon_w, icon_h, 0x1A1D24)
        drawRect(x, y, icon_w, 4, app.color) -- Цветовой акцент приложения
        
        -- Кнопка запуска
        if button(app.name, x + 10, y + 20, icon_w - 20, 40) then
            print("Launching " .. app.bin)
            exec(app.bin)
        end
    end

    -- 4. Рисуем нижнюю панель задач (Taskbar)
    local taskbar_y = 728
    local taskbar_h = 40
    drawGradient(0, taskbar_y, 1024, taskbar_h, 0x1B1F2A, 0x13161E, true)
    drawRect(0, taskbar_y, 1024, 1, 0x2E3440) -- Тонкая разделительная верхняя линия

    -- 5. Обработка клика по кнопке "Пуск"
    if button("Equinox", 10, taskbar_y + 5, 90, 30) then
        is_menu_open = not is_menu_open
        if is_menu_open then
            menu_anim_target = 1.0
        else
            menu_anim_target = 0.0
        end
        animTo(start_menu_anim, menu_anim_target)
    end

    -- 6. Отрисовка правого системного трея (Uptime / Часы + RAM)
    -- Расчет аптайма
    local uptime_sec = getUptime()
    local hours = math.floor(uptime_sec / 3600)
    local minutes = math.floor((uptime_sec % 3600) / 60)
    local seconds = math.floor(uptime_sec % 60)
    local clock_str = string.format("%02d:%02d:%02d", hours, minutes, seconds)

    -- Расчет RAM
    local used_mem, total_mem = getMemInfo()
    local used_mb = math.floor(used_mem / (1024 * 1024))
    local total_mb = math.floor(total_mem / (1024 * 1024))
    local ram_str = string.format("RAM: %d MB / %d MB", used_mb, total_mb)

    -- Выводим системную информацию на панель задач
    drawText(ram_str, 720, taskbar_y + 14, 0x8A8E9B)
    drawText(clock_str, 940, taskbar_y + 14, 0xD8DEE9)

    -- 7. Отрисовка выдвижного меню "Пуск" (если оно частично или полностью открыто)
    if menu_progress > 0.001 then
        local menu_w = 260
        local menu_h = 320
        local menu_x = 10
        
        -- Плавное выдвижение снизу вверх: вычисляем Y на основе текущего прогресса анимации
        local menu_y = math.floor(768 - (menu_progress * (768 - 400)))

        -- Задний фон меню
        drawGradient(menu_x, menu_y, menu_w, menu_h, 0x222735, 0x181B24, true)
        drawRect(menu_x, menu_y, menu_w, 2, 0x4A8DFD) -- Тонкая синяя линия сверху

        -- Текст заголовка в меню
        drawText("EQUINOX OS MENU", menu_x + 20, menu_y + 20, 0xFFFFFF)
        drawRect(menu_x + 20, menu_y + 45, menu_w - 40, 1, 0x2E3440) -- Разделитель

        -- Список программ внутри меню
        local list_start_y = menu_y + 60
        for i, app in ipairs(apps) do
            if button(app.name, menu_x + 20, list_start_y + (i-1)*38, menu_w - 40, 30) then
                exec(app.bin)
                -- Закрываем меню после запуска для удобства
                is_menu_open = false
                animTo(start_menu_anim, 0.0)
            end
        end

        -- Нижний статус-бар в меню Пуск
        local bar_y = menu_y + menu_h - 40
        drawRect(menu_x, bar_y, menu_w, 1, 0x2E3440)
        drawText("System Status: Operational", menu_x + 20, bar_y + 14, 0x4A8DFD)
    end
end