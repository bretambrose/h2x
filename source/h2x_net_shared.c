#include <h2x_net_shared.h>

#include <h2x_stream.h>
#include <h2x_connection.h>
#include <h2x_hash_table.h>
#include <h2x_log.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <string.h>

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
        H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Removed connection %d from epoll", connection->fd);
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

    if(pthread_mutex_lock(thread->finished_connection_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Processing thread failed to acquire finished connection lock");
        return;
    }

    last_connection->intrusive_chains[H2X_ICT_PENDING_CLOSE] = *(thread->finished_connections);
    *(thread->finished_connections) = last_connection;

    pthread_mutex_unlock(thread->finished_connection_lock);

    thread->intrusive_chains[H2X_ICT_PENDING_CLOSE] = NULL;
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

        connection->socket_state.last_event_mask = event_mask;

        // assumption: receiving any epoll event on the socket implies either
        // connected or error/failure
        connection->socket_state.has_connected = true;

        H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Received epoll event on fd %d with mask %d", connection->fd, event_mask);

        if(event_mask & (EPOLLERR | EPOLLHUP))
        {
            // give up completely, but try and fill out some error state but doing a read
            // to get errno set
            char fake_buffer[1];
            read(connection->fd, fake_buffer, sizeof(fake_buffer));
            connection->socket_state.io_error = errno;
            connection->socket_state.has_remote_hungup = true;

            H2X_LOG(H2X_LOG_LEVEL_INFO, "Connection %d errored with error %d", connection->fd, errno);

            h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_CLOSE);
            continue;
        }

        if(event_mask & EPOLLRDHUP)
        {
            // go to read-only mode
            connection->socket_state.has_remote_hungup = true;
            H2X_LOG(H2X_LOG_LEVEL_INFO, "Connection %d received remote hangup", connection->fd);
        }

        if(event_mask & (EPOLLIN | EPOLLPRI))
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
        bool should_close_connection = false;

        H2X_LOG(H2X_LOG_LEVEL_TRACE, "Connection %d attempting read", connection->fd);
        ssize_t count = read(connection->fd, read_buffer, READ_BUFFER_SIZE);
        if(count == -1)
        {
            if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Connection %d hit unexpected error %d during read", connection->fd, (int) errno);
                connection->socket_state.io_error = errno;
                should_close_connection = true;
            }
            else
            {
                H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Connection %d out of data for read", connection->fd);
            }
        }
        else if(count == 0)
        {
            H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Connection %d appears to be closed by remote peer with no further data", connection->fd);
            connection->socket_state.has_remote_hungup = true;
            should_close_connection = true;
        }
        else
        {
            H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Received %u bytes on connection %d", (uint32_t) count, connection->fd);
            h2x_connection_on_data_received(connection, read_buffer, count);
            connection->bytes_read += count;
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
        bool should_close_connection = false;
        bool is_write_finished = false;
        bool should_attempt_to_write = !connection->socket_state.has_remote_hungup && connection->socket_state.has_connected;

        H2X_LOG(H2X_LOG_LEVEL_TRACE, "Connection %d considering write (remote_hungup=%d, has_connected=%d)", connection->fd,
                (int)connection->socket_state.has_remote_hungup, (int)connection->socket_state.has_connected);

        h2x_connection_pump_outbound_frame(connection);
        if(should_attempt_to_write && connection->current_outbound_frame)
        {
            uint32_t write_size = connection->current_outbound_frame->size - connection->current_outbound_frame_read_position;
            assert(write_size > 0);

            H2X_LOG(H2X_LOG_LEVEL_TRACE, "Connection %d has outbound data (size %u) and is able to write", connection->fd, write_size);

            ssize_t count = write(connection->fd, connection->current_outbound_frame->raw_data, write_size);

            is_write_finished = count == write_size;
            if(count >= 0)
            {
                H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Connection %d wrote %u bytes", connection->fd, (uint32_t)count);
                connection->current_outbound_frame_read_position += count;
                connection->bytes_written += count;
            }
            else
            {
                if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                {
                    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Connection %d hit unexpected error %d during write", connection->fd, (int) errno);
                    should_close_connection = true;
                    connection->socket_state.io_error = errno;
                }
                else
                {
                    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Connection %d out of space to write", connection->fd);
                }
            }
        }
        else
        {
            is_write_finished = true;
        }


        if(should_close_connection)
        {
            h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_CLOSE);
        }

        if(is_write_finished || should_close_connection || !should_attempt_to_write)
        {
            h2x_connection_remove_from_intrusive_chain(write_connection_ptr, H2X_ICT_PENDING_WRITE);
        }
        else
        {
            write_connection_ptr = &((*write_connection_ptr)->intrusive_chains[H2X_ICT_PENDING_WRITE]);
        }
    }
}

void process_new_connections(struct h2x_connection* connections, int epoll_fd, struct h2x_hash_table* connection_table, bool done)
{
    struct epoll_event event;
    struct h2x_connection* connection = connections;
    while(connection)
    {
        struct h2x_thread* thread = connection->owner;

        bool should_close_connection = false;
        if(!done)
        {
            event.data.ptr = connection;
            // don't need to explicitly subscribe to EPOLLERR and EPOLLHUP
            event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR | EPOLLOUT | EPOLLRDHUP | EPOLLHUP;

            int ret_val = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connection->fd, &event);
            if (ret_val == -1)
            {
                H2X_LOG(H2X_LOG_LEVEL_INFO, "Unable to register connection %d with thread %u epoll instance", connection->fd, thread->thread_id);
                should_close_connection = true;
            }
            else
            {
                H2X_LOG(H2X_LOG_LEVEL_INFO, "Added connection %d to thread %u epoll instance", connection->fd, thread->thread_id);
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

        struct h2x_connection* next_connection = connection->next_new_connection;
        connection->next_new_connection = NULL;
        connection = next_connection;
    }
}

void process_new_requests(struct h2x_request* requests)
{
    struct h2x_request* request = requests;
    while(request)
    {
        struct h2x_connection* connection = request->connection;
        struct h2x_thread *thread = connection->owner;

        request->stream_id = h2x_connection_create_outbound_stream(connection, request->user_data);
        (*(connection->on_stream_headers_received))(connection, &request->header_list, request->stream_id, request->user_data);
        h2x_push_headers(connection, request->stream_id, &request->header_list);

        struct h2x_request* next_request = request->next;

        request->next = thread->inprogress_requests;
        thread->inprogress_requests = request;

        request = next_request;
    }
}

#define BODY_BUFFER_SIZE 8192

void process_inprogress_requests(struct h2x_thread* thread)
{
    struct h2x_request** request_ptr = &thread->inprogress_requests;
    uint8_t body_buffer[BODY_BUFFER_SIZE];

    while(*request_ptr)
    {
        struct h2x_request* request = *request_ptr;
        struct h2x_connection* connection = request->connection;
        uint32_t bytes_written = 0;

        bool is_request_finished = (*(connection->on_stream_data_needed))(connection, request->stream_id, body_buffer, BODY_BUFFER_SIZE, &bytes_written, request->user_data);

        h2x_push_data_segment(connection, request->stream_id, body_buffer, bytes_written, is_request_finished);

        if(is_request_finished)
        {
            // TODO: do something for a finished request rather than just let it leak
            *request_ptr = (*request_ptr)->next;
        }
        else
        {
            request_ptr = &((*request_ptr)->next);
        }
    }
}

void *h2x_processing_thread_function(void * arg)
{
    struct h2x_thread* self = arg;

    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to create epoll instance");
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
            H2X_LOG(H2X_LOG_LEVEL_TRACE, "Finished a single read/write pass");
        }

        // optionally combine these two lock/unlock pairs into one
        struct h2x_connection* new_connections = NULL;
        struct h2x_request* new_requests = NULL;
        h2x_thread_poll_quit_state(self, &done);
        h2x_thread_poll_new_requests_and_connections(self, &new_connections, &new_requests);

        process_new_connections(new_connections, epoll_fd, connection_table, done);
        process_new_requests(new_requests);
        process_inprogress_requests(self);

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
