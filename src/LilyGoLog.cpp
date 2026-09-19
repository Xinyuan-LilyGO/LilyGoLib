#include "LilyGoLog.h"

#if defined(ARDUINO) && LILYGO_DEBUG_ENABLED
void lilygo_log_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Serial.vprintf(format, args);
    va_end(args);
}

void lilygo_log_message(const char *level, const char *tag, const char *format, ...)
{
    Serial.printf("[%s] %s: ", level, tag);
    va_list args;
    va_start(args, format);
    Serial.vprintf(format, args);
    va_end(args);
    Serial.println();
}
#endif
