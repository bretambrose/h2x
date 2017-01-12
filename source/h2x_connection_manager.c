
#include <h2x_connection_manager.h>
#include <h2x_stream.h>
#include <h2x_connection.h>
#include <h2x_log.h>
#include <h2x_net_shared.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int h2x_connection_manager_init(struct h2x_options *options, struct h2x_connection_manager* connection_manager)
{
    connection_manager->options = options;
    connection_manager->finished_connections = NULL;
    connection_manager->next_thread_id = 1;

    if(pthread_mutex_init(&connection_manager->finished_connection_lock, NULL))
    {
        return -1;
    }

    uint32_t i;
    struct h2x_thread_node** thread_node = &connection_manager->processing_threads;

    for(i = 0; i < options->threads; ++i)
    {
        struct h2x_thread* thread = h2x_thread_new(options, h2x_processing_thread_function, connection_manager->next_thread_id++);
        h2x_thread_set_finished_connection_channel(thread, &connection_manager->finished_connection_lock, &connection_manager->finished_connections);

        *thread_node = malloc(sizeof(struct h2x_thread_node));
        (*thread_node)->thread = thread;
        (*thread_node)->next = NULL;

        thread_node = &((*thread_node)->next);

        H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Adding thread %u to connection manager", thread->thread_id);
    }

    return 0;
}

int h2x_connection_manager_cleanup(struct h2x_connection_manager* connection_manager)
{
    if(!connection_manager)
    {
        return 0;
    }

    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Shutting down connection manager threads");

    /* tell everyone to quit */
    struct h2x_thread_node* thread_node = connection_manager->processing_threads;
    while(thread_node)
    {
        struct h2x_thread* thread = thread_node->thread;

        H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Sending quit signal to thread %u", thread->thread_id);

        pthread_mutex_lock(&thread->quit_lock);
        thread->should_quit = true;
        pthread_mutex_unlock(&thread->quit_lock);

        thread_node = thread_node->next;
    }

    /* wait for everyone to quit */
    thread_node = connection_manager->processing_threads;
    while(thread_node)
    {
        struct h2x_thread* thread = thread_node->thread;

        H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Waiting for thread %u to quit", thread->thread_id);

        pthread_join(thread->thread, NULL);

        thread_node = thread_node->next;
    }

    /* cleanup threads and thread_nodes */
    thread_node = connection_manager->processing_threads;
    while(thread_node)
    {
        struct h2x_thread_node* next_node = thread_node->next;
        struct h2x_thread* thread = thread_node->thread;

        h2x_thread_cleanup(thread);
        free(thread_node);

        thread_node = next_node;
    }

    connection_manager->processing_threads = NULL;

    h2x_connection_manager_pump_closed_connections(connection_manager);

    pthread_mutex_destroy(&connection_manager->finished_connection_lock);

    return 0;
}

struct h2x_connection* h2x_connection_manager_add_connection(struct h2x_connection_manager* connection_manager, int fd)
{
    struct h2x_thread *add_thread = NULL;
    uint32_t lowest_count = 0;
    struct h2x_thread_node *thread_node = connection_manager->processing_threads;
    while (thread_node)
    {
        struct h2x_thread *thread = thread_node->thread;
        uint32_t thread_connection_count = thread->connection_count;
        if (thread_connection_count <= lowest_count &&
            thread_connection_count < thread->options->connections_per_thread)
        {
            add_thread = thread;
            lowest_count = thread_connection_count;
        }

        thread_node = thread_node->next;
    }

    if (add_thread == NULL)
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, "No available threads with room for a connection.");
        close(fd);
        return NULL;
    }

    struct h2x_connection* new_connection = malloc(sizeof(struct h2x_connection));
    h2x_connection_init(new_connection, add_thread, fd, add_thread->options->mode);

    if (h2x_thread_add_connection(add_thread, new_connection))
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, "Something went very wrong in h2x_thread_add_connection");
        h2x_connection_cleanup(new_connection); // TODO connection cleanup is F'ed up
        close(fd);
    }
    else
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, "Added connection %d to thread %u", fd, add_thread->thread_id);
    }

    return new_connection;
}

void h2x_connection_manager_pump_closed_connections(struct h2x_connection_manager* manager)
{
    struct h2x_connection* finished_connections = NULL;

    if(pthread_mutex_lock(&manager->finished_connection_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Unable to lock connection manager finished connections");
        return;
    }

    finished_connections = manager->finished_connections;
    manager->finished_connections = NULL;

    pthread_mutex_unlock(&manager->finished_connection_lock);

    struct h2x_connection* connection = finished_connections;
    while(connection)
    {
        struct h2x_connection *next_connection = connection->intrusive_chains[H2X_ICT_PENDING_CLOSE];

        H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Cleaning up connection %d", connection->fd);
        H2X_LOG(H2X_LOG_LEVEL_INFO, "Connection %d final socket state - event_mask:%u, socket_error:%d, remote_hungup:%d, has_connected:%d",
                connection->fd, connection->socket_state.last_event_mask, connection->socket_state.io_error,
                (int) connection->socket_state.has_remote_hungup, (int) connection->socket_state.has_connected);

        h2x_connection_cleanup(connection);
        close(connection->fd);
        free(connection);

        connection = next_connection;
    }
}

void h2x_connection_manager_list_threads(struct h2x_connection_manager* manager)
{
    struct h2x_thread_node* thread_node = manager->processing_threads;
    while(thread_node)
    {
        struct h2x_thread* thread = thread_node->thread;

        fprintf(stderr, "thread %u - %u connections\n", thread->thread_id, thread->connection_count);

        thread_node = thread_node->next;
    }
}

struct h2x_connection* h2x_connection_manager_add_client_connection(struct h2x_connection_manager* manager, char* address_string, int port)
{
    struct sockaddr_in dest_addr;

    //Create socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        return NULL;
    }

    if(h2x_make_socket_nonblocking(socket_fd))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to make client connection socket non blocking");
        close(socket_fd);
        return NULL;
    }

    dest_addr.sin_addr.s_addr = inet_addr(address_string);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    //Connect to remote server
    int ret_val = connect(socket_fd, (struct sockaddr *)&dest_addr , sizeof(dest_addr));
    if(ret_val < 0 && errno != EINPROGRESS)
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to connect to %s:%d  errno=%d", address_string, port, (int)errno);
        close(socket_fd);
        return NULL;
    }

    return h2x_connection_manager_add_connection(manager, socket_fd);
}