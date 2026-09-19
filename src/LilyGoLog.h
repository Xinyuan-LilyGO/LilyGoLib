#pragma once

#include "LilyGoLogConfig.h"

#if LILYGO_DEBUG_ENABLED
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ARDUINO
void lilygo_log_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void lilygo_log_message(const char *level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
#else
// Header-only desktop backend avoids pulling hardware sources into the emulator.
static inline void lilygo_log_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
static inline void lilygo_log_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

static inline void lilygo_log_message(const char *level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
static inline void lilygo_log_message(const char *level, const char *tag, const char *format, ...)
{
    printf("[%s] %s: ", level, tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
}
#endif

#ifdef __cplusplus
}

#ifdef ARDUINO
#include <Arduino.h>
#define LILYGO_LOG_PRINT(...) do { Serial.print(__VA_ARGS__); } while (0)
#define LILYGO_LOG_PRINTLN(...) do { Serial.println(__VA_ARGS__); } while (0)
#define LILYGO_LOG_WRITE(...) do { Serial.write(__VA_ARGS__); } while (0)
#else
#include <iostream>
namespace lilygo_log_detail {
inline void println() { std::cout << '\n'; }
template <typename T> inline void println(const T &value) { std::cout << value << '\n'; }
}
#define LILYGO_LOG_PRINT(value) do { std::cout << (value); } while (0)
#define LILYGO_LOG_PRINTLN(...) do { lilygo_log_detail::println(__VA_ARGS__); } while (0)
#define LILYGO_LOG_WRITE(value) do { putchar((unsigned char)(value)); } while (0)
#endif
#endif

#define LILYGO_LOG_PRINTF(...) do { lilygo_log_printf(__VA_ARGS__); } while (0)
#define LILYGO_LOG_E(...) do { lilygo_log_message("E", __func__, __VA_ARGS__); } while (0)
#define LILYGO_LOG_W(...) do { lilygo_log_message("W", __func__, __VA_ARGS__); } while (0)
#define LILYGO_LOG_I(...) do { lilygo_log_message("I", __func__, __VA_ARGS__); } while (0)
#define LILYGO_LOG_D(...) do { lilygo_log_message("D", __func__, __VA_ARGS__); } while (0)
#define LILYGO_LOG_V(...) do { lilygo_log_message("V", __func__, __VA_ARGS__); } while (0)
#else
#define LILYGO_LOG_PRINTF(...) do {} while (0)
#define LILYGO_LOG_PRINT(...) do {} while (0)
#define LILYGO_LOG_PRINTLN(...) do {} while (0)
#define LILYGO_LOG_WRITE(...) do {} while (0)
#define LILYGO_LOG_E(...) do {} while (0)
#define LILYGO_LOG_W(...) do {} while (0)
#define LILYGO_LOG_I(...) do {} while (0)
#define LILYGO_LOG_D(...) do {} while (0)
#define LILYGO_LOG_V(...) do {} while (0)
#endif

// Use for diagnostic-only helpers whose arguments or bodies do extra work.
#if LILYGO_DEBUG_ENABLED
#define LILYGO_LOG_ONLY(...) do { __VA_ARGS__; } while (0)
#else
#define LILYGO_LOG_ONLY(...) do {} while (0)
#endif
