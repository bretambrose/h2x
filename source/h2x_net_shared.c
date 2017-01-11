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

struct cleanup_context {
    int epoll_fd;
    struct h2x_connection_node** closed_connections_ptr;
};

static void cleanup_connection_table_entry(void *data, void* context)
{
    struct h2x_connection* connection = data;
    struct cleanup_context* clean_context = context;

    int ret_val = epoll_ctl(clean_context->epoll_fd, EPOLL_CTL_DEL, connection->fd, NULL);

    struct h2x_connection_node** node_ptr = clean_context->closed_connections_ptr;

    struct h2x_connection_node* node = malloc(sizeof(struct h2x_connection_node));
    node->connection = connection;
    node->next = *node_ptr;
    *node_ptr = node;
}

static void release_closed_connections(struct h2x_thread* thread, struct h2x_connection_node* connections)
{
    if(!connections)
    {
        return;
    }

    struct h2x_connection_node *last_node = connections;
    while(last_node->next != NULL)
    {
        last_node = last_node->next;
    }

    pthread_mutex_lock(thread->finished_connection_lock);

    last_node->next = *(thread->finished_connections);
    *(thread->finished_connections) = last_node;

    pthread_mutex_unlock(thread->finished_connection_lock);
}

#define READ_BUFFER_SIZE 8192

static bool process_epoll_event(struct epoll_event *event)
{
    struct h2x_connection* connection = event->data.ptr;
    int event_fd = connection->fd;
    int event_mask = event->events;
    if((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) || (!(event_mask & (EPOLLIN | EPOLLPRI | EPOLLOUT))))
    {
        fprintf (stderr, "epoll error. events=%u\n", event_mask);
        return false;
    }

    bool should_close_connection = false;

    // try and determine if succesfully established
    /*
     * TODO
    if(connection->??)
    {
        ??;
    }*/

    if(event_mask & EPOLLIN)
    {
        uint8_t input_buffer[READ_BUFFER_SIZE];

        while(1)
        {
            ssize_t count = read(event_fd, input_buffer, READ_BUFFER_SIZE);
            if(count == -1)
            {
                if(errno != EAGAIN)
                {
                    should_close_connection = true;
                }
                break;
            }
            else if(count == 0)
            {
                should_close_connection = true;
                break;
            }
            else
            {
                h2x_connection_on_data_received(connection, input_buffer, count);
            }
        }
    }

    if(event_mask & EPOLLOUT)
    {
        should_close_connection |= h2x_connection_write_outbound_data(connection);
    }

    return should_close_connection;
}

void *h2x_processing_thread_function(void * arg)
{
    struct h2x_thread* self = arg;
    struct epoll_event event;
    int ret_val;
    struct h2x_connection_node* closed_connections = NULL;

    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        fprintf(stderr, "Unable to create epoll instance\n");
        return NULL;
    }

    uint32_t max_connections = self->options->connections_per_thread;
    struct epoll_event* events = calloc(max_connections, sizeof(struct epoll_event));
    struct h2x_hash_table* connection_table = (struct h2x_hash_table*)malloc(sizeof(struct h2x_hash_table));
    h2x_hash_table_init(connection_table, self->options->connections_per_thread, connection_hash_function);

    bool done = false;
    while(!done)
    {
        int event_count = epoll_wait(epoll_fd, events, max_connections, 0);
        int i;
        for(i = 0; i < event_count; i++)
        {
            bool should_close_connection = process_epoll_event(&events[i]);

            if(should_close_connection)
            {
                struct h2x_connection* connection = events[i].data.ptr;
                h2x_hash_table_remove(connection_table, connection->fd);
                ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connection->fd, NULL);

                struct h2x_connection_node* node = malloc(sizeof(struct h2x_connection_node));
                node->connection = connection;
                node->next = closed_connections;
                closed_connections = node;
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
                bool should_close_connection = false;
                struct h2x_connection* connection = (struct h2x_connection*)malloc(sizeof(struct h2x_connection));
                h2x_connection_init(connection, socket->fd, self->options->mode);

                if(!done)
                {
                    event.data.ptr = connection;
                    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR | EPOLLOUT | EPOLLRDHUP | EPOLLHUP;

                    ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket->fd, &event);
                    if (ret_val == -1)
                    {
                        fprintf(stderr, "Unable to register new socket with epoll instance\n");
                        should_close_connection = true;
                    }
                    else
                    {
                        h2x_hash_table_add(connection_table, connection);
                    }
                }
                else
                {
                    should_close_connection = true;
                }

                if(should_close_connection)
                {
                    struct h2x_connection_node* node = malloc(sizeof(struct h2x_connection_node));
                    node->connection = connection;
                    node->next = closed_connections;
                    closed_connections = node;
                }

                struct h2x_socket* old_socket = socket;
                socket = socket->next;
                free(old_socket);
            }
        }

        release_closed_connections(self, closed_connections);
        closed_connections = NULL;
    }

    struct cleanup_context processing_context;
    processing_context.epoll_fd = epoll_fd;
    processing_context.closed_connections_ptr = &closed_connections;

    h2x_hash_table_visit(connection_table, cleanup_connection_table_entry, &processing_context);
    h2x_hash_table_cleanup(connection_table);
    free(connection_table);

    release_closed_connections(self, closed_connections);

    free(events);
    close(epoll_fd);

    return NULL;
}
