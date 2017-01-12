#include <h2x_thread.h>

#include <h2x_connection.h>
#include <h2x_log.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

struct h2x_thread* h2x_thread_new(struct h2x_options* options, void *(*start_routine)(void *), uint32_t thread_id)
{
    struct h2x_thread* thread = malloc(sizeof(struct h2x_thread));

    thread->options = options;
    thread->thread_id = thread_id;
    thread->connection_count = 0;
    thread->epoll_fd = 0;
    thread->new_connections = NULL;
    thread->should_quit = false;
    thread->new_requests = NULL;
    thread->finished_connection_lock = NULL;
    thread->finished_connections = NULL;

    for(uint32_t i = 0; i < H2X_ICT_COUNT; ++i)
    {
        thread->intrusive_chains[i] = NULL;
    }

    if(pthread_mutex_init(&thread->new_connections_lock, NULL))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to initialize thread %u new connections mutex, errno = %d", thread_id, (int) errno);
        goto CLEANUP_THREAD;
    }

    if(pthread_mutex_init(&thread->quit_lock, NULL))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to initialize thread %u quit mutex, errno = %d", thread_id, (int) errno);
        goto CLEANUP_MUTEX1;
    }

    pthread_attr_t thread_attr;
    if(pthread_attr_init(&thread_attr))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to initialize thread %u thread attributes, errno = %d", thread_id, (int) errno);
        goto CLEANUP_MUTEX2;
    }

    if(pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to make thread %u joinable, errno = %d", thread_id, (int) errno);
        goto CLEANUP_THREAD_ATTR;
    }

    if(!pthread_create(&thread->thread, NULL, start_routine, thread))
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, "Successfully created thread %u", thread_id);
        return thread;
    }

    H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to create thread %u, errno = %d", thread_id, (int) errno);

CLEANUP_THREAD_ATTR:
    pthread_attr_destroy(&thread_attr);

CLEANUP_MUTEX2:
    pthread_mutex_destroy(&thread->quit_lock);

CLEANUP_MUTEX1:
    pthread_mutex_destroy(&thread->new_connections_lock);

CLEANUP_THREAD:
    free(thread);
    return NULL;
}

void h2x_thread_set_epoll_fd(struct h2x_thread* thread, int epoll_fd)
{
    thread->epoll_fd = epoll_fd;
}

void h2x_thread_set_finished_connection_channel(struct h2x_thread* thread,
                                                pthread_mutex_t* finished_connection_lock,
                                                struct h2x_connection** finished_connections)
{
    thread->finished_connection_lock = finished_connection_lock;
    thread->finished_connections = finished_connections;
}

void h2x_thread_cleanup(struct h2x_thread* thread)
{
    /* the child thread should have destroyed this already and nothing
     * new can have been added during the shutdown process
     */
    assert(thread->new_connections == NULL);

    pthread_mutex_destroy(&thread->quit_lock);
    pthread_mutex_destroy(&thread->new_connections_lock);

    free(thread);
}

int h2x_thread_add_connection(struct h2x_thread* thread, struct h2x_connection *connection)
{
    if (thread == NULL || connection == NULL)
    {
        return -1;
    }

    if(pthread_mutex_lock(&thread->new_connections_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to lock thread %u state in order to add connection %d, errno = %d", thread->thread_id, connection->fd, (int) errno);
        return -1;
    }

    H2X_LOG(H2X_LOG_LEVEL_INFO, "Adding new connection %d to thread %u", connection->fd, thread->thread_id);

    connection->next_new_connection = thread->new_connections;
    thread->new_connections = connection;
    ++thread->connection_count;

    pthread_mutex_unlock(&thread->new_connections_lock);
    return 0;
}

int h2x_thread_poll_new_connections(struct h2x_thread* thread, struct h2x_connection** new_connections)
{
    *new_connections = NULL;

    if(pthread_mutex_lock(&thread->new_connections_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to lock thread %u state in order to poll new connections, errno = %d", thread->thread_id, (int) errno);
        return -1;
    }

    *new_connections = thread->new_connections;
    thread->new_connections = NULL;

    pthread_mutex_unlock(&thread->new_connections_lock);
    return 0;
}

int h2x_thread_poll_quit_state(struct h2x_thread* thread, bool* quit_state)
{
    if(pthread_mutex_lock(&thread->quit_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to lock thread %u in order to poll quit state, errno = %d", thread->thread_id, (int) errno);
        return -1;
    }

    *quit_state = thread->should_quit;

    pthread_mutex_unlock(&thread->quit_lock);
    return 0;
}