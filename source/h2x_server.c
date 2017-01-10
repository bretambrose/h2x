
#include <h2x_server.h>

#include <h2x_buffer.h>
#include <h2x_command.h>
#include <h2x_connection_manager.h>
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

static int handle_quit_command(int argc, char** argv)
{
    fprintf(stderr, "Shutting down server...\n");
    g_quit = true;
    return 0;
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
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret_val));
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
        fprintf(stderr, "Failed to make listener socket non blocking\n");
        close(socket_fd);
        return -1;
    }

    if(listen(socket_fd, SOMAXCONN))
    {
        fprintf(stderr, "Failed to set socket as passive listener\n");
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

#define LISTENER_EVENT_COUNT 2
#define READ_BUFFER_SIZE 8192

void h2x_do_server(struct h2x_options* options)
{
    int listener_fd = -1, epoll_fd = -1;
    int ret_val;
    struct epoll_event* events = NULL;
    struct epoll_event event;

    memset(&event, 0, sizeof(struct epoll_event));

    struct h2x_buffer stdin_buffer;
    h2x_buffer_init(200, &stdin_buffer);

    listener_fd = create_listener_socket(options);
    if(listener_fd < 0)
    {
        fprintf(stderr, "Unable to create and bind listener socket\n");
        return;
    }

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

    /* Add the listener socket */
    event.data.fd = listener_fd;
    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR;
    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_fd, &event);
    if(ret_val == -1)
    {
        fprintf(stderr, "Unable to register listener socket with epoll instance\n");
        goto CLEANUP;
    }

    /* Add stdin for server commands */
    event.data.fd = STDIN_FILENO;
    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR;
    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);
    if(ret_val == -1)
    {
        fprintf(stderr, "Unable to register stdin with epoll instance\n");
        goto CLEANUP;
    }

    events = calloc(LISTENER_EVENT_COUNT, sizeof(struct epoll_event));

    struct h2x_connection_manager* manager = create_connection_manager(options);

    g_quit = false;
    while(!g_quit)
    {
        int event_count, i;

        event_count = epoll_wait(epoll_fd, events, LISTENER_EVENT_COUNT, -1);
        for(i = 0; i < event_count; i++)
        {
            int event_fd = events[i].data.fd;
            int event_mask = events[i].events;
            if((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) || (!(event_mask & (EPOLLIN | EPOLLPRI))))
            {
                fprintf (stderr, "epoll error. events=%u\n", events[i].events);
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
                            fprintf(stderr, "Error accepting connection: %u\n", errno);
                            goto CLEANUP;
                        }

                        break;
                    }

                    ret_val = h2x_make_socket_nonblocking(incoming_fd);
                    if(ret_val == -1)
                    {
                        fprintf(stderr, "Failed to make incoming connection non-blocking\n");
                        goto CLEANUP;
                    }

                    h2x_connection_manager_add_connection(manager, incoming_fd);
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

                h2x_command_process(&stdin_buffer, server_commands, SERVER_COMMAND_COUNT);
            }
        }
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

