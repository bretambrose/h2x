#include <h2x_net_shared.h>

#include <h2x_connection.h>
#include <h2x_hash_table.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>


int h2x_make_socket_nonblocking(int socket_fd)
{
  int flags, ret_val;

  flags = fcntl(socket_fd, F_GETFL, 0);
  if(flags == -1)
  {
    return -1;
  }

  flags |= O_NONBLOCK;
  ret_val = fcntl (socket_fd, F_SETFL, flags);
  if(ret_val == -1)
  {
    return -1;
  }

  return 0;
}

bool h2x_is_little_endian_system()
{
    int i = 1;
    return (int)*((unsigned char *)&i)==1;
}

void h2x_set_integer_as_big_endian(uint8_t* to_set, uint32_t int_value)
{
    uint16_t data_index = 0;
    uint16_t int_index = 0;

    if(h2x_is_little_endian_system())
    {
        int_index = sizeof(uint32_t) - 1;
    }

    uint8_t* int_as_buffer = (uint8_t*)&int_value;

    for(;data_index < sizeof(uint32_t); ++data_index, --int_index)
    {
        to_set[data_index] = int_as_buffer[int_index];
    }
}

static uint32_t connection_hash_function(void* data)
{
    struct h2x_connection* connection = data;
    return connection->fd;
}

static void cleanup_connection_table_entry(void *data)
{
    struct h2x_connection* connection = data;

    close(connection->fd);
    h2x_connection_cleanup(connection);
}

#define READ_BUFFER_SIZE 8192

void *h2x_processing_thread_function(void * arg)
{
    struct h2x_thread* self = arg;
    struct epoll_event event;
    int ret_val;

    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        fprintf(stderr, "Unable to create epoll instance\n");
        return NULL;
    }

    uint32_t max_connections = self->options->connections_per_thread;
    struct epoll_event* events = calloc(max_connections, sizeof(struct epoll_event));
    struct h2x_hash_table* connection_table = h2x_hash_table_init(self->options->connections_per_thread, connection_hash_function);

    uint8_t input_buffer[READ_BUFFER_SIZE];
    bool done = false;
    while(!done)
    {
        int event_count = epoll_wait(epoll_fd, events, max_connections, 0);
        int i;
        for(i = 0; i < event_count; i++)
        {
            struct h2x_connection* connection = events[i].data.ptr;
            int event_fd = connection->fd;
            int event_mask = events[i].events;
            if((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) || (!(event_mask & (EPOLLIN | EPOLLPRI))))
            {
                fprintf (stderr, "epoll error. events=%u\n", events[i].events);
                continue;
            }

            bool close_connection = false;
            while(1)
            {
                ssize_t count = read(event_fd, input_buffer, READ_BUFFER_SIZE);
                if(count == -1)
                {
                    if(errno != EAGAIN)
                    {
                        close_connection = true;
                    }
                    break;
                }
                else if(count == 0)
                {
                    close_connection = true;
                    break;
                }
                else
                {
                    h2x_connection_on_data_received(connection, input_buffer, count);

                    // server-only echo to stdout for now
                    if(connection->mode == H2X_MODE_SERVER)
                    {
                        write(1, input_buffer, count);
                    }
                }
            }

            if(close_connection)
            {
                close(connection->fd);
                h2x_hash_table_remove(connection_table, connection->fd);
                h2x_connection_cleanup(connection);
            }
        }

        // begin read from shared state
        if(pthread_mutex_lock(&self->state_lock))
        {
            continue;
        }

        done = self->should_quit;
        struct h2x_socket* new_connections = self->new_connections;
        self->new_connections = NULL;
        pthread_mutex_unlock(&self->state_lock);
        // end read from shared state

        if(new_connections)
        {
            struct h2x_socket* socket = new_connections;
            while(socket)
            {
                if(!done)
                {
                    struct h2x_connection *connection = h2x_connection_init(socket->fd, self->options->mode);

                    event.data.ptr = connection;
                    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR;

                    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket->fd, &event);
                    if (ret_val == -1)
                    {
                        fprintf(stderr, "Unable to register new socket with epoll instance\n");
                        h2x_connection_cleanup(connection);
                        close(socket->fd);
                    }

                    h2x_hash_table_add(connection_table, connection);
                }
                else
                {
                    close(socket->fd);
                }

                struct h2x_socket* old_socket = socket;
                socket = socket->next;
                free(old_socket);
            }
        }
    }

    h2x_hash_table_visit(connection_table, cleanup_connection_table_entry);

    free(events);
    close(epoll_fd);

    h2x_hash_table_cleanup(connection_table);

    return NULL;
}
