
#ifndef H2X_COMMAND_H
#define H2X_COMMAND_H

#include <stdbool.h>

struct h2x_buffer;

struct command_def {
    char* command;
    int required_arguments;
    bool capture_all_last_required;
    int (*handler)(int, char**, void*);
    char* help;
};

void h2x_command_process(struct h2x_buffer* buffer, struct command_def* command_definitions, int command_count, void* context);

#endif //H2X_COMMAND_H
