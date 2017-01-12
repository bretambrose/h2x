#include <h2x_client.h>

#include <h2x_buffer.h>
#include <h2x_command.h>
#include <h2x_connection_manager.h>
#include <h2x_net_shared.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <arpa/inet.h>
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
    fprintf(stderr, "Shutting down client...\n");
    g_quit = true;
    return 0;
}

static int handle_connect_command(int argc, char** argv, void* context)
{
    struct sockaddr_in dest_addr;

    //Create socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        return -1;
    }

    if(h2x_make_socket_nonblocking(socket_fd))
    {
        fprintf(stderr, "Failed to make client connection socket non blocking\n");
        close(socket_fd);
        return -1;
    }

    dest_addr.sin_addr.s_addr = inet_addr(argv[0]);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(atoi(argv[1]));

    //Connect to remote server
    int ret_val = connect(socket_fd, (struct sockaddr *)&dest_addr , sizeof(dest_addr));
    if(ret_val < 0 && errno != EINPROGRESS)
    {
        fprintf(stderr, "Failed to connect to %s:%s  errno=%d\n", argv[0], argv[1], (int)errno);
        close(socket_fd);
        return -1;
    }

    struct h2x_connection_manager* manager = context;
    h2x_connection_manager_add_connection(manager, socket_fd);

    return 0;
}

struct command_def client_commands[] = {
    { "quit", 0, false, handle_quit_command, "shuts down the client" },
    { "connect", 2, false, handle_connect_command, "[dest ip] [dest port] - attempts to connect to an h2x server process" },
    { "list_threads", 0, false, h2x_command_handle_list_threads, "lists all connection manager threads" },
    { "list_connections", 1, false, h2x_command_handle_list_connections, "[thread_id] - lists all connections within a thread" },
    { "describe_thread", 1, false, h2x_command_handle_describe_thread, "[thread_id] - dumps detailed information about a thread" },
    { "describe_connection", 1, false, h2x_command_handle_describe_connection, "[connection_id] [thread_id] - dumps details infromation about a connection" }
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
        fprintf(stderr, "Unable to make stdin nonblocking\n");
        goto CLEANUP;
    }

    epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        fprintf(stderr, "Unable to create epoll instance\n");
        goto CLEANUP;
    }

    /* Add stdin for client commands */
    event.data.fd = STDIN_FILENO;
    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR;
    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);
    if(ret_val == -1)
    {
        fprintf(stderr, "Unable to register stdin with epoll instance\n");
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
                fprintf (stderr, "epoll error. events=%u\n", events[i].events);
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
