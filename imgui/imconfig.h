#pragma once

// --- СОВМЕСТИМОСТЬ С FREESTANDING ОКРУЖЕНИЕМ EQUINOX OS ---

#include <stdint.h>
#include <stddef.h>

// 1. Быстрые математические функции для интерфейса
inline float imgui_fabsf(float x) {
    return x < 0.0f ? -x : x;
}

// Быстрое вычисление степени на битовых хаках (для слайдеров ImGui идеальная точность не требуется)
inline float imgui_powf(float base, float exp) {
    if (exp == 0.0f) return 1.0f;
    if (exp == 1.0f) return base;
    if (base <= 0.0f) return 0.0f;
    union { float d; int i; } u = { base };
    u.i = (int)(exp * (float)(u.i - 1064866805) + 1064866805);
    return u.d;
}

// Быстрое вычисление логарифма
inline float imgui_logf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float d; int i; } u = { x };
    return (float)(u.i - 1064866805) * 8.262958288196508e-8f;
}

// Перенаправляем макросы ImGui на наши функции
#define ImFabs(X) imgui_fabsf(X)
#define fabsf(X)  imgui_fabsf(X)
#define powf(X,Y) imgui_powf(X,Y)
#define logf(X)   imgui_logf(X)

// 2. Локальная реализация qsort (сортировка вставками на уровне байт)
// Заменяет стандартный библиотечный qsort для сортировки слоев окон ImGui
inline void imgui_qsort(void* base, size_t num, size_t size, int (*compar)(const void*, const void*)) {
    if (num < 2) return;
    uint8_t* array = (uint8_t*)base;
    for (size_t i = 1; i < num; i++) {
        for (size_t j = i; j > 0; j--) {
            uint8_t* a = array + j * size;
            uint8_t* b = array + (j - 1) * size;
            if (compar(a, b) < 0) {
                // Побайтовый обмен (swap)
                for (size_t k = 0; k < size; k++) {
                    uint8_t tmp = a[k];
                    a[k] = b[k];
                    b[k] = tmp;
                }
            } else {
                break;
            }
        }
    }
}

#define ImQsort imgui_qsort
#define qsort imgui_qsort