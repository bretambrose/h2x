#include <h2x_client.h>

#include <h2x_buffer.h>
#include <h2x_command.h>
#include <h2x_connection.h>
#include <h2x_connection_manager.h>
#include <h2x_headers.h>
#include <h2x_log.h>
#include <h2x_net_shared.h>
#include <h2x_options.h>
#include <h2x_request.h>
#include <h2x_thread.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define CLIENT_EVENT_COUNT 1
#define READ_BUFFER_SIZE 8192
#define STDIN_BUFFER_SIZE 512

bool g_quit;

static int handle_quit_command(int argc, char** argv, void* context)
{
    H2X_LOG(H2X_LOG_LEVEL_INFO, "Shutting down client...");
    g_quit = true;
    return 0;
}

static int handle_connect_command(int argc, char** argv, void* context)
{
    struct h2x_connection_manager* manager = context;
    h2x_connection_manager_add_client_connection(manager, argv[0], atoi(argv[1]));

    return 0;
}

void response_header_callback(struct h2x_connection* connection, struct h2x_header_list* headers, uint32_t stream_id, void* user_data)
{
    h2x_header_reset_iter(headers);

    H2X_LOG(H2X_LOG_LEVEL_INFO, "Received response headers on stream %u, connection %d:", stream_id, connection->fd)
    struct h2x_header* header = h2x_header_next(headers);
    while(header)
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, " %s = %s", header->name, header->value)
        header = h2x_header_next(headers);
    }
}

void response_body_callback(struct h2x_connection* connection, uint8_t* data, uint32_t length, uint32_t stream_id, bool lastFrame, void* user_data)
{
    H2X_LOG(H2X_LOG_LEVEL_INFO, "Received body data on stream %u, connection %d:", stream_id, connection->fd)

    char *raw_data_string = malloc(length + 1);
    raw_data_string[length] = 0;
    memcpy(raw_data_string, data, length);

    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Raw data: %s", raw_data_string);

    free(raw_data_string);
}

struct fake_request
{
    FILE* body_file;
};

static bool fake_request_on_stream_data_needed(struct h2x_connection* connection, uint32_t stream_id, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_written, void* user_data)
{
    H2X_LOG(H2X_LOG_LEVEL_INFO, "Fake request data needed callback - stream_id %u, connection %d", stream_id, connection->fd);

    struct fake_request *request = user_data;
    if (!request->body_file)
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, "Fake request stream_id %u, connection %d has no body", stream_id, connection->fd);
        bytes_written = 0;
        return true;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)buffer_size, request->body_file);

    *bytes_written = (uint32_t) bytes_read;

    if(bytes_read == 0)
    {
        fclose(request->body_file);
        request->body_file = NULL;
    }

    return bytes_read == 0;
}

static void fake_request_stream_error_callback(struct h2x_connection* connection, h2x_connection_error error, uint32_t stream_id, void* user_data)
{
    H2X_LOG(H2X_LOG_LEVEL_ERROR, "Fake request stream error callback - stream_id %u, connection %d, error %u", stream_id, connection->fd, (uint32_t)error);
}

static int handle_request_command(int argc, char** argv, void* context)
{
    struct h2x_connection_manager *manager = context;
    struct h2x_connection *connection = h2x_connection_manager_add_client_connection(manager, argv[0], atoi(argv[1]));

    h2x_connection_set_stream_data_needed_callback(connection, fake_request_on_stream_data_needed);
    h2x_connection_set_stream_error_callback(connection, fake_request_stream_error_callback);
    h2x_connection_set_stream_headers_receieved_callback(connection, response_header_callback);
    h2x_connection_set_stream_body_receieved_callback(connection, response_body_callback);

    struct fake_request *user_data = malloc(sizeof(struct fake_request));
    user_data->body_file = fopen(argv[3], "r");

    struct h2x_request *request = malloc(sizeof(struct h2x_request));
    h2x_request_init(request, connection, user_data);

    FILE *header_file = fopen(argv[2], "r");
    if (header_file)
    {
        char header_name_buffer[256];
        char header_value_buffer[256];
        bool done = false;
        while(!done)
        {
            int match_count = fscanf(header_file, "%s %s", header_name_buffer, header_value_buffer);
            if(match_count != 2)
            {
                done = true;
                break;
            }

            H2X_LOG(H2X_LOG_LEVEL_INFO, "Fake request adding header pair %s=%s", header_name_buffer, header_value_buffer);

            struct h2x_header header;
            header.name = strdup(header_name_buffer);
            header.value = strdup(header_value_buffer);

            h2x_header_list_append(&request->header_list, header);
        }
    }

    h2x_connection_add_request(connection, request);

    return 0;
}

struct command_def client_commands[] = {
    { "quit", 0, false, handle_quit_command, "shuts down the client" },
    { "connect", 2, false, handle_connect_command, "[dest ip] [dest port] - attempts to connect to an h2x server process" },
    { "request", 4, false, handle_request_command, "[dest ip] [dest port] [header_file] [body_file] - attempts to connect to an h2x server process and send a request built from the header and body files" },
    { "list_threads", 0, false, h2x_command_handle_list_threads, "lists all connection manager threads" },
    { "list_connections", 1, false, h2x_command_handle_list_connections, "[thread_id] - lists all connections within a thread" },
    { "describe_thread", 1, false, h2x_command_handle_describe_thread, "[thread_id] - dumps detailed information about a thread" },
    { "describe_connection", 1, false, h2x_command_handle_describe_connection, "[connection_id] [thread_id] - dumps details information about a connection" }
};

#define CLIENT_COMMAND_COUNT ((uint32_t)(sizeof(client_commands) / sizeof(struct command_def)))


static struct h2x_connection_manager* create_connection_manager(struct h2x_options* options)
{
    struct h2x_connection_manager* manager = malloc(sizeof(struct h2x_connection_manager));
    h2x_connection_manager_init(options, manager);

    return manager;
}

void h2x_do_client(struct h2x_options* options)
{
    int epoll_fd = -1;
    int ret_val;
    struct epoll_event* events = NULL;
    struct epoll_event event;

    memset(&event, 0, sizeof(struct epoll_event));

    struct h2x_buffer stdin_buffer;
    h2x_buffer_init(STDIN_BUFFER_SIZE, &stdin_buffer);

    struct h2x_connection_manager* manager = create_connection_manager(options);
    events = calloc(CLIENT_EVENT_COUNT, sizeof(struct epoll_event));

    if(h2x_make_socket_nonblocking(STDIN_FILENO))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Unable to make stdin nonblocking");
        goto CLEANUP;
    }

    epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to create epoll instance");
        goto CLEANUP;
    }

    /* Add stdin for client commands */
    event.data.fd = STDIN_FILENO;
    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR;
    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);
    if(ret_val == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to register stdin with epoll instance");
        goto CLEANUP;
    }

    g_quit = false;
    while(!g_quit)
    {
        int event_count, i;

        event_count = epoll_wait(epoll_fd, events, CLIENT_EVENT_COUNT, -1);
        for(i = 0; i < event_count; i++)
        {
            int event_fd = events[i].data.fd;
            int event_mask = events[i].events;
            if((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) || (!(event_mask & (EPOLLIN | EPOLLPRI))))
            {
                H2X_LOG(H2X_LOG_LEVEL_ERROR, "epoll error. events=%u", events[i].events);
                goto CLEANUP;
            }

            if(event_fd == STDIN_FILENO)
            {
                char input_buffer[READ_BUFFER_SIZE];

                while(1)
                {
                    ssize_t count = read(event_fd, input_buffer, READ_BUFFER_SIZE);
                    if(count <= -1)
                    {
                        break;
                    }

                    h2x_buffer_write(input_buffer, count, &stdin_buffer);
                }

                h2x_command_process(&stdin_buffer, client_commands, CLIENT_COMMAND_COUNT, manager);
            }
        }

        h2x_connection_manager_pump_closed_connections(manager);
    }

CLEANUP:
    h2x_connection_manager_cleanup(manager);
    h2x_buffer_free(&stdin_buffer);
    free(events);

    if(epoll_fd != -1)
    {
        close(epoll_fd);
    }
}
