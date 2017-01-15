#include <h2x_thread.h>

#include <h2x_connection.h>
#include <h2x_log.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

struct h2x_thread* h2x_thread_new(struct h2x_options* options, void *(*start_routine)(void *), uint32_t thread_id)
{
    struct h2x_thread* thread = malloc(sizeof(struct h2x_thread));

    thread->options = options;
    thread->thread_id = thread_id;
    thread->epoll_fd = 0;
    thread->new_connections = NULL;
    atomic_init(&thread->should_quit, false);
    thread->new_requests = NULL;
    thread->finished_connection_lock = NULL;
    thread->finished_connections = NULL;

    for(uint32_t i = 0; i < H2X_ICT_COUNT; ++i)
    {
        thread->intrusive_chains[i] = NULL;
    }

    thread->epoll_fd = epoll_create1(0);
    if(thread->epoll_fd == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_FATAL, "Unable to create epoll instance");
        goto CLEANUP_THREAD;
    }

    if(pthread_mutex_init(&thread->new_data_lock, NULL))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to initialize thread %u new connections mutex, errno = %d", thread_id, (int) errno);
        goto CLEANUP_EPOLL;
    }

    pthread_attr_t thread_attr;
    if(pthread_attr_init(&thread_attr))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to initialize thread %u thread attributes, errno = %d", thread_id, (int) errno);
        goto CLEANUP_MUTEX;
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

CLEANUP_MUTEX:
    pthread_mutex_destroy(&thread->new_data_lock);

CLEANUP_EPOLL:
    close(thread->epoll_fd);

CLEANUP_THREAD:
    free(thread);

    return NULL;
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

    pthread_mutex_destroy(&thread->new_data_lock);

    free(thread);
}

int h2x_thread_add_connection(struct h2x_thread* thread, struct h2x_connection *connection)
{
    if (thread == NULL || connection == NULL)
    {
        return -1;
    }

    struct epoll_event event;
    event.data.ptr = connection;
    // don't need to explicitly subscribe to EPOLLERR and EPOLLHUP
    event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR | EPOLLOUT | EPOLLRDHUP | EPOLLHUP;

    int ret_val = epoll_ctl(thread->epoll_fd, EPOLL_CTL_ADD, connection->fd, &event);
    if (ret_val == -1)
    {
        H2X_LOG(H2X_LOG_LEVEL_INFO, "Unable to register connection %d with thread %u epoll instance", connection->fd, thread->thread_id);
        return -1;
    }

    return 0;
}

int h2x_thread_poll_new_requests_and_connections(struct h2x_thread* thread, struct h2x_connection** new_connections, struct h2x_request** new_requests)
{
    *new_connections = NULL;

    if(pthread_mutex_lock(&thread->new_data_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to lock thread %u state in order to poll new connections, errno = %d", thread->thread_id, (int) errno);
        return -1;
    }

    *new_connections = thread->new_connections;
    thread->new_connections = NULL;

    *new_requests = thread->new_requests;
    thread->new_requests = NULL;

    pthread_mutex_unlock(&thread->new_data_lock);
    return 0;
}

int h2x_thread_poll_quit_state(struct h2x_thread* thread, bool* quit_state)
{
    *quit_state = atomic_load(&thread->should_quit);

    return 0;
}

int h2x_thread_add_request(struct h2x_thread* thread, struct h2x_request* request)
{
    if (thread == NULL || request == NULL)
    {
        return -1;
    }

    if(pthread_mutex_lock(&thread->new_data_lock))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Failed to lock thread %u state in order to add request to connection %d, errno = %d", thread->thread_id, request->connection->fd, (int) errno);
        return -1;
    }

    H2X_LOG(H2X_LOG_LEVEL_INFO, "Adding new request for connection %d to thread %u", request->connection->fd, thread->thread_id);

    request->next = thread->new_requests;
    thread->new_requests = request;

    pthread_mutex_unlock(&thread->new_data_lock);
    return 0;
}
