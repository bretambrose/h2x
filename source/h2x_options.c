#include <h2x_options.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void intialize_options_defaults(struct h2x_options* options)
{
    options->threads = 1;
    options->connections_per_thread = 1000;
    options->port = 3333;
    options->mode = H2X_MODE_NONE;
    options->security_protocol = H2X_SECURITY_NONE;
    options->log_level = H2X_LOG_LEVEL_DEBUG;
    options->log_dest = H2X_LOG_DEST_STDERR;
    options->log_filename = NULL;
    options->sync_logging = true;
}

static int parse_h2x_mode(char** args, struct h2x_options* options)
{
    if(strcmp(args[1], "server") == 0)
    {
        options->mode = H2X_MODE_SERVER;
        return 0;
    }
    else if(strcmp(args[1], "client") == 0)
    {
        options->mode = H2X_MODE_CLIENT;
        return 0;
    }

    fprintf(stderr, "Unknown argument for --mode option: %s\n", args[1]);
    return -1;
}

static int parse_h2x_security_protocol(char** args, struct h2x_options* options)
{
    if(strcmp(args[1], "none") == 0)
    {
        options->security_protocol = H2X_SECURITY_NONE;
        return 0;
    }
    else if(strcmp(args[1], "tls") == 0)
    {
        options->security_protocol = H2X_SECURITY_TLS;
        return 0;
    }

    fprintf(stderr, "Unknown argument for --mode option: %s\n", args[1]);
    return -1;
}

static int parse_h2x_port(char** args, struct h2x_options* options)
{
    options->port = atoi(args[1]);
    if(options->port == 0)
    {
        fprintf(stderr, "Invalid value for --port option: %s\n", args[1]);
        return -1;
    }

    return 0;
}

static int parse_h2x_threads(char** args, struct h2x_options* options)
{
    options->threads = atoi(args[1]);
    if(options->threads == 0)
    {
        fprintf(stderr, "Invalid value for --threads option: %s\n", args[1]);
        return -1;
    }

    return 0;
}

static int parse_h2x_conn(char** args, struct h2x_options* options)
{
    options->connections_per_thread = atoi(args[1]);
    if(options->connections_per_thread == 0)
    {
        fprintf(stderr, "Invalid value for --conn option: %s\n", args[1]);
        return -1;
    }

    return 0;
}

static int parse_h2x_log_level(char** args, struct h2x_options* options)
{
    options->log_level = string_to_h2x_log_level(args[1]);

    return 0;
}

static int parse_h2x_log_dest(char** args, struct h2x_options* options)
{
    options->log_dest = string_to_h2x_log_dest(args[1]);

    return 0;
}

static int parse_h2x_log_filename(char** args, struct h2x_options* options)
{
    options->log_filename = strdup(args[1]);

    return 0;
}

static int parse_h2x_sync_logging(char** args, struct h2x_options* options)
{
    options->sync_logging = true;

    return 0;
}

typedef struct {
    char* option_name;
    uint32_t argument_count;
    int (*parse_function)(char**, struct h2x_options*);
    char* option_help;
} h2x_option_parser;

h2x_option_parser option_parsers[] = {
    { "--mode", 1, parse_h2x_mode, "(required) what mode to run the program in [server|client]" },
    { "--security", 1, parse_h2x_security_protocol, "what connection security protocol to use [none|tls]" },
    { "--port", 1, parse_h2x_port, "(server required) what port to listen for connections on" },
    { "--threads", 1, parse_h2x_threads, "(server) number of threads to process connections on; defaults to 1" },
    { "--conn", 1, parse_h2x_conn, "(server) maximum number of connections per thread; defaults to 1000" },
    { "--log_level", 1, parse_h2x_log_level, "sets the logging level for the process [Off | Fatal | Error | Warn | Info | Debug | Trace]" },
    { "--log_dest", 1, parse_h2x_log_dest, "sets the logging destination for the process [None | Stderr | File]" },
    { "--log_filename", 1, parse_h2x_log_filename, "when logging to a file, sets the filename (defaults to h2x.log)" },
    { "--sync_logging", 0, parse_h2x_sync_logging, "synchronize all access to the log destination (slow)" }
};

#define H2X_OPTION_COUNT (sizeof(option_parsers) / sizeof(h2x_option_parser))

struct h2x_options* h2x_options_copy(struct h2x_options* source)
{
    struct h2x_options* options = malloc(sizeof(struct h2x_options));
    *options = *source;
    if(source->log_filename)
    {
        options->log_filename = strdup(source->log_filename);
    }

    return options;
}

int h2x_options_init(struct h2x_options* options, int argc, char** argv)
{
    intialize_options_defaults(options);

    int arg_index = 1;
    while(arg_index < argc)
    {
        bool found_handler = false;
        uint32_t i;
        for(i = 0; i < H2X_OPTION_COUNT; ++i)
        {
            if(strcmp(option_parsers[i].option_name, argv[arg_index]) == 0)
            {
                found_handler = true;
                uint32_t arg_count = option_parsers[i].argument_count;
                if(arg_index + arg_count >= (uint32_t)argc || (*option_parsers[i].parse_function)(argv + arg_index, options) != 0)
                {
                    return -1;
                }

                arg_index += arg_count + 1;
                break;
            }
        }

        if(found_handler == false)
        {
            fprintf(stderr, "Unknown option: %s\n", argv[arg_index]);
            return -1;
        }
    }

    return 0;
}

void h2x_options_cleanup(struct h2x_options* options)
{
    if(options->log_filename)
    {
        free(options->log_filename);
    }
}

void h2x_print_usage(char *program_name)
{
    printf("Usage:\n");
    printf("%s <options>\n", program_name);

    uint32_t i;
    for(i = 0; i < H2X_OPTION_COUNT; ++i)
    {
        printf("  %s - %s\n", option_parsers[i].option_name, option_parsers[i].option_help);
    }
}
