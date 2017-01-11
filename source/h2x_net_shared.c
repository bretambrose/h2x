#include <h2x_net_shared.h>

#include <h2x_connection.h>
#include <h2x_hash_table.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <assert.h>
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

static void cleanup_connection_table_entry(void *data, void* context)
{
    struct h2x_connection* connection = data;

    h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_CLOSE);
}

static void release_closed_connections(struct h2x_thread* thread)
{
    if(!thread->intrusive_chains[H2X_ICT_PENDING_CLOSE])
    {
        return;
    }

    // remove all the finished connections from our epoll instance
    struct h2x_connection *connection = thread->intrusive_chains[H2X_ICT_PENDING_CLOSE];
    while(connection)
    {
        epoll_ctl(thread->epoll_fd, EPOLL_CTL_DEL, connection->fd, NULL);
        connection = connection->intrusive_chains[H2X_ICT_PENDING_CLOSE];
    }

    struct h2x_connection *last_connection = thread->intrusive_chains[H2X_ICT_PENDING_CLOSE];
    while(last_connection->intrusive_chains[H2X_ICT_PENDING_CLOSE] != NULL)
    {
        assert(last_connection->intrusive_chains[H2X_ICT_PENDING_READ] == NULL);
        assert(last_connection->intrusive_chains[H2X_ICT_PENDING_WRITE] == NULL);

        last_connection = last_connection->intrusive_chains[H2X_ICT_PENDING_CLOSE];
    }

    assert(last_connection->intrusive_chains[H2X_ICT_PENDING_READ] == NULL);
    assert(last_connection->intrusive_chains[H2X_ICT_PENDING_WRITE] == NULL);

    pthread_mutex_lock(thread->finished_connection_lock);

    last_connection->intrusive_chains[H2X_ICT_PENDING_CLOSE] = *(thread->finished_connections);
    *(thread->finished_connections) = last_connection;

    pthread_mutex_unlock(thread->finished_connection_lock);
}

void build_pending_read_write_chains(struct h2x_thread *thread, struct epoll_event* events, int event_count)
{
    int i;
    for(i = 0; i < H2X_ICT_COUNT; ++i)
    {
        assert(thread->intrusive_chains[i] == NULL);
    }

    for(i = 0; i < event_count; i++)
    {
        struct epoll_event* event = events + i;
        struct h2x_connection* connection = event->data.ptr;
        int event_mask = event->events;

        if((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) || (!(event_mask & (EPOLLIN | EPOLLPRI | EPOLLOUT))))
        {
            fprintf (stderr, "epoll error. events=%u\n", event_mask);
            continue; // TODO: how to handle this?
        }

        if(event_mask & EPOLLIN)
        {
            h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_READ);
        }

        if(event_mask & EPOLLOUT)
        {
            h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_WRITE);
        }
    }
}

#define READ_BUFFER_SIZE 8192

void process_pending_read_chain(struct h2x_thread* thread)
{
    uint8_t read_buffer[READ_BUFFER_SIZE];

    // one round of reads
    struct h2x_connection** read_connection_ptr = &thread->intrusive_chains[H2X_ICT_PENDING_READ];
    while(*read_connection_ptr != NULL)
    {
        bool is_read_finished = false;
        struct h2x_connection* connection = *read_connection_ptr;

        bool should_close_connection = h2x_connection_validate(connection) == false;

        ssize_t count = read(connection->fd, read_buffer, READ_BUFFER_SIZE);
        if(count == -1)
        {
            if(errno != EAGAIN)
            {
                should_close_connection = true;
            }
        }
        else if(count == 0)
        {
            should_close_connection = true;
        }
        else
        {
            h2x_connection_on_data_received(connection, read_buffer, count);
        }

        if(count < READ_BUFFER_SIZE)
        {
            is_read_finished = true;
        }

        if(should_close_connection)
        {
            h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_CLOSE);
        }

        if(is_read_finished || should_close_connection)
        {
            h2x_connection_remove_from_intrusive_chain(read_connection_ptr, H2X_ICT_PENDING_READ);
        }
        else
        {
            read_connection_ptr = &((*read_connection_ptr)->intrusive_chains[H2X_ICT_PENDING_READ]);
        }
    }
}

void process_pending_write_chain(struct h2x_thread* thread)
{
    // one round of writes
    struct h2x_connection** write_connection_ptr = &thread->intrusive_chains[H2X_ICT_PENDING_WRITE];
    while(*write_connection_ptr != NULL)
    {
        struct h2x_connection* connection = *write_connection_ptr;
        bool should_close_connection = h2x_connection_validate(connection) == false;
        bool is_write_finished = false;

        h2x_connection_pump_outbound_frame(connection);
        if(connection->current_outbound_frame)
        {
            uint32_t write_size = connection->current_outbound_frame->size - connection->current_outbound_frame_read_position;
            ssize_t count = write(connection->fd, connection->current_outbound_frame->raw_data, write_size);

            if(count >= 0)
            {
                is_write_finished = count == write_size;
                connection->current_outbound_frame_read_position += count;
            }
            else
            {
                is_write_finished = true;
                if(errno != EAGAIN)
                {
                    should_close_connection = true;
                }
            }
        }

        if(should_close_connection)
        {
            h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_CLOSE);
        }

        if(is_write_finished || should_close_connection)
        {
            h2x_connection_remove_from_intrusive_chain(write_connection_ptr, H2X_ICT_PENDING_WRITE);
        }
        else
        {
            write_connection_ptr = &((*write_connection_ptr)->intrusive_chains[H2X_ICT_PENDING_WRITE]);
        }
    }
}

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

    h2x_thread_set_epoll_fd(self, epoll_fd);

    uint32_t max_connections = self->options->connections_per_thread;
    struct epoll_event* events = calloc(max_connections, sizeof(struct epoll_event));
    struct h2x_hash_table* connection_table = (struct h2x_hash_table*)malloc(sizeof(struct h2x_hash_table));
    h2x_hash_table_init(connection_table, self->options->connections_per_thread, connection_hash_function);

    bool done = false;
    while(!done)
    {
        int event_count = epoll_wait(epoll_fd, events, max_connections, 0);
        build_pending_read_write_chains(self, events, event_count);

        while(self->intrusive_chains[H2X_ICT_PENDING_READ] || self->intrusive_chains[H2X_ICT_PENDING_WRITE])
        {
            process_pending_read_chain(self);
            process_pending_write_chain(self);
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
                h2x_connection_init(connection, self, socket->fd, self->options->mode);

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
                    h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_CLOSE);
                }

                struct h2x_socket* old_socket = socket;
                socket = socket->next;
                free(old_socket);
            }
        }

        release_closed_connections(self);
    }

    h2x_hash_table_visit(connection_table, cleanup_connection_table_entry, self);
    h2x_hash_table_cleanup(connection_table);
    free(connection_table);

    release_closed_connections(self);

    free(events);
    close(epoll_fd);

    return NULL;
}
