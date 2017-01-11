

#ifndef H2X_OPTIONS_H
#define H2X_OPTIONS_H

#include <h2x_enum_types.h>

#include <stdint.h>

struct h2x_options {
    h2x_mode mode;
    h2x_security_protocol_type security_protocol;
    uint16_t port;
    uint32_t threads;
    uint32_t connections_per_thread;
} h2x_options;

int h2x_parse_options(int argc, char** argv, struct h2x_options* options);
void h2x_print_usage(char *program_name);

#endif // H2X_OPTIONS_H
