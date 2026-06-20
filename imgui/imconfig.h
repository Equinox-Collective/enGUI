#pragma once

// --- СОВМЕСТИМОСТЬ С FREESTANDING ОКРУЖЕНИЕМ EQUINOX OS ---

// Оборачиваем заголовки SDK в extern "C", заставляя Си++ компилятор
// использовать Си-связывание для всех функций стандартной библиотеки
extern "C" {
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
}

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Отключаем ассерты для предотвращения синтаксических ошибок компилятора
#define IM_ASSERT(_EXPR) ((void)0)

// Импортируем vsprintf из SDK и пишем inline-заглушки для Unix-функций шелла
extern "C" {
    int vsprintf(char* str, const char* format, va_list arg);

    // Локальные безвредные заглушки для обхода требований fork/waitpid в ImGui
    inline int fork(void) { return -1; }
    inline int waitpid(int, int*, int) { return -1; }
    inline int execvp(const char*, char* const*) { return -1; }
}

// 1. Быстрые математические функции для рендеринга GUI (float)
inline float imgui_fabsf(float x) { return x < 0.0f ? -x : x; }
inline float imgui_ceilf(float x) { int i = (int)x; return (float)(i < x ? i + 1 : i); }
inline float imgui_floorf(float x) { int i = (int)x; return (float)(i > x ? i - 1 : i); }
inline float imgui_fmodf(float x, float y) { return y == 0.0f ? 0.0f : x - (float)((int)(x / y)) * y; }

inline float imgui_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float xhalf = 0.5f * x;
    union { float f; int i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    u.f = u.f * (1.5f - xhalf * u.f * u.f);
    return x * u.f;
}

inline float imgui_cosf(float x) {
    const float PI = 3.1415926535f;
    const float TWO_PI = 6.2831853071f;
    float g = x < 0.0f ? -x : x;
    int q = (int)(g / TWO_PI);
    g -= (float)q * TWO_PI;
    if (g > PI) g -= TWO_PI;
    if (g < -PI) g += TWO_PI;
    float x2 = g * g;
    return 1.0f - x2 * 0.5f + (x2 * x2) * 0.0416666f - (x2 * x2 * x2) * 0.0013888f;
}

inline float imgui_sinf(float x) {
    return imgui_cosf(x - 1.57079632679f);
}

inline float imgui_acosf(float x) {
    if (x < -1.0f) x = -1.0f; if (x > 1.0f) x = 1.0f;
    float negate = (x < 0.0f) ? 1.0f : 0.0f;
    float abs_x = x < 0.0f ? -x : x;
    float ret = -0.0187293f;
    ret = ret * abs_x + 0.0742610f;
    ret = ret * abs_x - 0.2121144f;
    ret = ret * abs_x + 1.5707288f;
    ret = ret * imgui_sqrtf(1.0f - abs_x);
    return negate ? 3.1415926535f - ret : ret;
}

inline float imgui_atan2f(float y, float x) {
    if (x == 0.0f && y == 0.0f) return 0.0f;
    float abs_y = y < 0.0f ? -y : y;
    float abs_x = x < 0.0f ? -x : x;
    float angle;
    if (abs_x >= abs_y) {
        float r = y / x;
        float r2 = r * r;
        angle = r * (0.97239411f - 0.19194795f * r2);
        if (x < 0.0f) angle += (y < 0.0f) ? -3.1415926535f : 3.1415926535f;
    } else {
        float r = x / y;
        float r2 = r * r;
        angle = r * (0.97239411f - 0.19194795f * r2);
        angle = (y < 0.0f) ? -1.57079632679f - angle : 1.57079632679f - angle;
    }
    return angle;
}

inline float imgui_powf(float base, float exp) {
    if (exp == 0.0f) return 1.0f;
    if (exp == 1.0f) return base;
    if (base <= 0.0f) return 0.0f;
    union { float d; int i; } u = { base };
    u.i = (int)(exp * (float)(u.i - 1064866805) + 1064866805);
    return u.d;
}

inline float imgui_logf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float d; int i; } u = { x };
    return (float)(u.i - 1064866805) * 8.262958288196508e-8f;
}

// 2. Двойные версии математики (double)
inline double imgui_fabs(double x) { return x < 0.0 ? -x : x; }
inline double imgui_pow(double x, double y) { return (double)imgui_powf((float)x, (float)y); }
inline double imgui_log(double x) { return (double)imgui_logf((float)x); }

#define fabsf(X)    imgui_fabsf(X)
#define fmodf(X,Y)  imgui_fmodf(X,Y)
#define ceilf(X)    imgui_ceilf(X)
#define floorf(X)   imgui_floorf(X)
#define cosf(X)     imgui_cosf(X)
#define sinf(X)     imgui_sinf(X)
#define acosf(X)    imgui_acosf(X)
#define atan2f(Y,X) imgui_atan2f(Y,X)
#define sqrtf(X)    imgui_sqrtf(X)
#define powf(X,Y)   imgui_powf(X,Y)
#define logf(X)     imgui_logf(X)

#define fabs(X)     imgui_fabs(X)
#define pow(X,Y)    imgui_pow(X,Y)
#define log(X)      imgui_log(X)

// 3. Легковесный sscanf для парсинга конфигурации ImGui (поддерживает %d, %f, %x)
inline int imgui_sscanf(const char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int count = 0;
    const char* f = format;
    const char* s = str;
    while (*f && *s) {
        if (*f == '%') {
            f++;
            if (*f == 'd') {
                int* val = va_arg(args, int*);
                int sign = 1;
                if (*s == '-') { sign = -1; s++; }
                int v = 0;
                while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
                *val = v * sign;
                count++;
            } else if (*f == 'f') {
                float* val = va_arg(args, float*);
                float rez = 0, dec = 10;
                bool point = false, neg = false;
                if (*s == '-') { neg = true; s++; }
                while ((*s >= '0' && *s <= '9') || *s == '.') {
                    if (*s == '.') { point = true; s++; continue; }
                    int d = *s - '0';
                    if (!point) rez = rez * 10.0f + d;
                    else { rez = rez + (float)d / dec; dec *= 10.0f; }
                    s++;
                }
                *val = neg ? -rez : rez;
                count++;
            } else if (*f == 'x') {
                unsigned int* val = va_arg(args, unsigned int*);
                unsigned int v = 0;
                while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')) {
                    v *= 16;
                    if (*s >= '0' && *s <= '9') v += (*s - '0');
                    else if (*s >= 'a' && *s <= 'f') v += 10 + (*s - 'a');
                    else if (*s >= 'A' && *s <= 'F') v += 10 + (*s - 'A');
                    s++;
                }
                *val = v;
                count++;
            }
            f++;
        } else {
            if (*f == *s) { s++; }
            f++;
        }
    }
    va_end(args);
    return count;
}
#define sscanf imgui_sscanf

// 4. Легковесный atof (строка -> double)
inline double imgui_atof(const char* s) {
    double rez = 0, dec = 10;
    bool point = false, neg = false;
    if (*s == '-') { neg = true; s++; }
    while (*s) {
        if (*s == '.') { point = true; s++; continue; }
        int d = *s - '0';
        if (d >= 0 && d <= 9) {
            if (!point) rez = rez * 10.0 + d;
            else { rez = rez + (double)d / dec; dec *= 10.0; }
        }
        s++;
    }
    return neg ? -rez : rez;
}
#define atof imgui_atof

// 5. Безопасный snprintf на базе vsprintf из нашего SDK
inline int imgui_snprintf(char* str, size_t size, const char* format, ...) {
    (void)size;
    va_list args;
    va_start(args, format);
    int ret = vsprintf(str, format, args);
    va_end(args);
    return ret;
}
#define snprintf imgui_snprintf

// 6. Локальная реализация qsort
inline void imgui_qsort(void* base, size_t num, size_t size, int (*compar)(const void*, const void*)) {
    if (num < 2) return;
    uint8_t* array = (uint8_t*)base;
    for (size_t i = 1; i < num; i++) {
        for (size_t j = i; j > 0; j--) {
            uint8_t* a = array + j * size;
            uint8_t* b = array + (j - 1) * size;
            if (compar(a, b) < 0) {
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