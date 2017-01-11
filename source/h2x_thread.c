#include <h2x_thread.h>

#include <assert.h>
#include <stdlib.h>

struct h2x_thread* h2x_thread_new(struct h2x_options* options, void *(*start_routine)(void *))
{
    struct h2x_thread* thread = malloc(sizeof(struct h2x_thread));

    thread->options = options;
    thread->new_connections = NULL;
    thread->should_quit = false;
    thread->pending_read_chain = NULL;
    thread->pending_write_chain = NULL;
    thread->pending_close_chain = NULL;

    if(pthread_mutex_init(&thread->state_lock, NULL))
    {
        goto CLEANUP_THREAD;
    }

    pthread_attr_t thread_attr;
    if(pthread_attr_init(&thread_attr))
    {
        goto CLEANUP_MUTEX;
    }

    if(pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE))
    {
        goto CLEANUP_THREAD_ATTR;
    }

    if(!pthread_create(&thread->thread, NULL, start_routine, thread))
    {
        return thread;
    }

CLEANUP_THREAD_ATTR:
    pthread_attr_destroy(&thread_attr);

CLEANUP_MUTEX:
    pthread_mutex_destroy(&thread->state_lock);

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

    pthread_mutex_destroy(&thread->state_lock);

    free(thread);
}

int h2x_thread_add_connection(struct h2x_thread* thread, int fd)
{
    struct h2x_socket* new_connection = malloc(sizeof(struct h2x_socket));
    new_connection->fd = fd;

    if(pthread_mutex_lock(&thread->state_lock))
    {
        free(new_connection);
        return -1;
    }

    new_connection->next = thread->new_connections;
    thread->new_connections = new_connection;
    ++thread->connection_count;

    pthread_mutex_unlock(&thread->state_lock);
    return 0;
}

int h2x_thread_poll_new_connections(struct h2x_thread* thread, struct h2x_socket** new_connections)
{
    *new_connections = NULL;

    if(pthread_mutex_lock(&thread->state_lock))
    {
        return -1;
    }

    *new_connections = thread->new_connections;
    thread->new_connections = NULL;

    pthread_mutex_unlock(&thread->state_lock);
    return 0;
}
