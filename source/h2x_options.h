

#ifndef H2X_OPTIONS_H
#define H2X_OPTIONS_H

#include <h2x_enum_types.h>

#include <stdbool.h>
#include <stdint.h>

struct h2x_options {
    h2x_mode mode;
    h2x_security_protocol_type security_protocol;
    uint16_t port;
    uint32_t threads;
    uint32_t connections_per_thread;
    bool non_interactive;
    bool protocol_debug;

    h2x_log_level log_level;
    h2x_log_dest log_dest;
    char *log_filename;
    bool sync_logging;
} h2x_options;

int h2x_options_init(struct h2x_options* options, int argc, char** argv);
void h2x_options_cleanup(struct h2x_options* options);

void h2x_print_usage(char *program_name);

#endif // H2X_OPTIONS_H
