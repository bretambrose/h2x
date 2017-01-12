
#ifndef H2X_LOG_H
#define H2X_LOG_H

#include <h2x_enum_types.h>

#include <stdbool.h>

struct h2x_options;

extern h2x_log_level g_log_level;

void h2x_logging_init(struct h2x_options* options);
void h2x_logging_cleanup();

void h2x_log(h2x_log_level log_level, const char* format_str, ...);

#define H2X_LOG(level, ...) \
    { \
        if ( g_log_level >= level ) \
        { \
            h2x_log(level, __VA_ARGS__); \
        } \
    }

#endif //H2X_LOG_H
