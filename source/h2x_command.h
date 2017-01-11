
#ifndef H2X_COMMAND_H
#define H2X_COMMAND_H

#include <stdbool.h>
#include <stdint.h>

struct h2x_buffer;

struct command_def {
    char* command;
    int required_arguments;
    bool capture_all_last_required;
    int (*handler)(int, char**, void*);
    char* help;
};

void h2x_command_process(struct h2x_buffer* buffer, struct command_def* command_definitions, uint32_t command_count, void* context);


// common command handlers
int h2x_command_handle_list_threads(int argc, char** argv, void* context);
int h2x_command_handle_list_connections(int argc, char** argv, void* context);
int h2x_command_handle_describe_thread(int argc, char** argv, void* context);
int h2x_command_handle_describe_connection(int argc, char** argv, void* context);

#endif //H2X_COMMAND_H
