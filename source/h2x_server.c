
#include <h2x_server.h>

#include <h2x_buffer.h>
#include <h2x_command.h>
#include <h2x_connection.h>
#include <h2x_connection_manager.h>
#include <h2x_headers.h>
#include <h2x_log.h>
#include <h2x_net_shared.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>


bool g_quit;

static int handle_quit_command(int argc, char** argv, void* context)
{
    H2X_LOG(H2X_LOG_LEVEL_INFO, "Shutting down server...");
    g_quit = true;
    return 0;
}

void modified_echo_header_callback(struct h2x_connection* connection, struct h2x_header_list* headers, uint32_t stream_id, void* user_data)
{
    struct h2x_header_list* new_headers = malloc(sizeof(struct h2x_header_list));
    h2x_header_list_init(new_headers);

    h2x_header_reset_iter(headers);

    H2X_LOG(H2X_LOG_LEVEL_INFO, "Server received request headers on stream %u, connection %d:", stream_id, connection->fd)

    struct h2x_header* header = h2x_header_next(headers);
    while(header)
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, " %s = %s", header->name, header->value)

        struct h2x_header new_header;
        new_header.name = strdup(header->name);
        new_header.value = strdup(header->value);

        h2x_header_list_append(new_headers, new_header);
        header = h2x_header_next(headers);
    }

    struct h2x_header response_code_header;
    response_code_header.name = strdup("response-code");

    char buffer[256];
    sprintf(buffer, "%d", 200);
    response_code_header.value = strdup(buffer);

    h2x_header_list_append(new_headers, response_code_header);

    h2x_push_headers(connection, stream_id, new_headers);
}

char *response_append_string = " is what you sent me";

void modified_echo_body_callback(struct h2x_connection* connection, uint8_t* data, uint32_t length, uint32_t stream_id, bool lastFrame, void* user_data)
{
    uint32_t response_size = length;
    if(lastFrame)
    {
        response_size += strlen(response_append_string);
    }

    if(response_size == 0)
    {
        return;
    }

    uint8_t* response_data = malloc(response_size);
    memcpy(response_data, data, length);
    if(lastFrame)
    {
        memcpy(response_data + length, response_append_string, strlen(response_append_string));
    }

    H2X_LOG(H2X_LOG_LEVEL_INFO, "Server received request body data on stream %u, connection %d:", stream_id, connection->fd)

    char *raw_data_string = malloc(length + 1);
    raw_data_string[length] = 0;
    memcpy(raw_data_string, data, length);

    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Raw data: %s", raw_data_string);

    free(raw_data_string);

    h2x_push_data_segment(connection, stream_id, response_data, response_size, lastFrame);
    if(lastFrame)
    {
        h2x_push_rst_stream(connection, stream_id, H2X_NO_ERROR);
    }

    free(response_data);
}

struct command_def server_commands[] = {
    { "quit", 0, false, handle_quit_command, "shuts down the server" }
};

#define SERVER_COMMAND_COUNT (sizeof(server_commands) / sizeof(struct command_def))

static struct h2x_connection_manager* create_connection_manager(struct h2x_options* options)
{
    struct h2x_connection_manager* manager = malloc(sizeof(struct h2x_connection_manager));
    h2x_connection_manager_init(options, manager);

    return manager;
}

static int create_listener_socket(struct h2x_options* options)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int ret_val, socket_fd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char port[20];
    sprintf(port, "%d", options->port);

    ret_val = getaddrinfo(NULL, port, &hints, &result);
    if(ret_val != 0)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Error %d (errno %d) calling getaddrinfo: %s", ret_val, errno, gai_strerror(ret_val));
        return -1;
    }

    for(rp = result; rp != NULL; rp = rp->ai_next)
    {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(socket_fd == -1)
        {
            continue;
        }

        ret_val = bind(socket_fd, rp->ai_addr, rp->ai_addrlen);
        if (ret_val == 0)
        {
            break;
        }

        close(socket_fd);
    }

    if(rp == NULL)
    {
        return -1;
    }

    freeaddrinfo(result);

    if(h2x_make_socket_nonblocking(socket_fd))
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Failed to make listener socket non blocking");
        close(socket_fd);
        return -1;
    }

    if(listen(socket_fd, SOMAXCONN))
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Failed to set socket as passive listener");
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

#define LISTENER_EVENT_COUNT 2
#define READ_BUFFER_SIZE 8192
#define STDIN_BUFFER_SIZE 512

void h2x_do_server(struct h2x_options* options)
{
    int listener_fd = -1, epoll_fd = -1;
    int ret_val;
    struct epoll_event* events = NULL;
    struct epoll_event event;

    memset(&event, 0, sizeof(struct epoll_event));

    struct h2x_buffer stdin_buffer;
    h2x_buffer_init(STDIN_BUFFER_SIZE, &stdin_buffer);

    events = calloc(LISTENER_EVENT_COUNT, sizeof(struct epoll_event));
    struct h2x_connection_manager* manager = create_connection_manager(options);

    listener_fd = create_listener_socket(options);
    if(listener_fd < 0)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to create and bind listener socket, errno = %d", errno);
        return;
    }

    if(h2x_make_socket_nonblocking(STDIN_FILENO))
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to make stdin nonblocking");
        goto CLEANUP;
    }

    epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to create epoll instance");
        goto CLEANUP;
    }

    /* Add the listener socket */
    event.data.fd = listener_fd;
    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR;
    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_fd, &event);
    if(ret_val == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to register listener socket with epoll instance");
        goto CLEANUP;
    }

    /* Add stdin for server commands */
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

        event_count = epoll_wait(epoll_fd, events, LISTENER_EVENT_COUNT, 10);
        for(i = 0; i < event_count; i++)
        {
            int event_fd = events[i].data.fd;
            int event_mask = events[i].events;
            if((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) || (!(event_mask & (EPOLLIN | EPOLLPRI))))
            {
                H2X_LOG(H2X_LOG_LEVEL_ERROR, "Unexpected epoll event mask %d on fd %d", event_mask, event_fd);
                goto CLEANUP;
            }

            if(event_fd == listener_fd)
            {
                while(1)
                {
                    struct sockaddr incoming_addr;
                    socklen_t incoming_len;
                    int incoming_fd;

                    incoming_len = sizeof(struct sockaddr);
                    incoming_fd = accept(event_fd, &incoming_addr, &incoming_len);
                    if(incoming_fd == -1)
                    {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                            H2X_LOG(H2X_LOG_LEVEL_ERROR, "Error accepting connection %d, errno = %d", incoming_fd, (int) errno);
                            goto CLEANUP;
                        }

                        break;
                    }

                    ret_val = h2x_make_socket_nonblocking(incoming_fd);
                    if(ret_val == -1)
                    {
                        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to make connection %d non-blocking, errno = %d", incoming_fd, (int) errno);
                        close(incoming_fd);
                        continue;
                    }

                    struct h2x_connection* server_connection = h2x_connection_manager_add_connection(manager, incoming_fd);
                    h2x_connection_set_stream_headers_receieved_callback(server_connection, modified_echo_header_callback);
                    h2x_connection_set_stream_body_receieved_callback(server_connection, modified_echo_body_callback);
                }
            }
            else
            {
                assert(event_fd == STDIN_FILENO);
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

                h2x_command_process(&stdin_buffer, server_commands, SERVER_COMMAND_COUNT, manager);
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

    if(listener_fd != -1)
    {
        close(listener_fd);
    }
}

