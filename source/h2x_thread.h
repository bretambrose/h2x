
#ifndef H2X_THREAD_H
#define H2X_THREAD_H

#include <h2x_enum_types.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct h2x_options;

struct h2x_socket {
    struct h2x_socket* next;
    int fd;
};

struct h2x_thread {
    struct h2x_options* options;    // const, thread-safe read
    pthread_t thread;               // const, thread-safe read
    uint32_t connection_count;      // non-const, only readable by connection manager thread
    int epoll_fd;

    pthread_mutex_t state_lock;     // lock for shared state between processing thread and connection manager
    struct h2x_socket* new_connections;  // shared state
    bool should_quit;                    // shared state

    pthread_mutex_t* finished_connection_lock;  // lock for global shared state between all processing threads and connection manager
    struct h2x_connection** finished_connections;

    struct h2x_connection* intrusive_chains[H2X_ICT_COUNT];
};

struct h2x_thread_node {
    struct h2x_thread_node* next;
    struct h2x_thread* thread;
};

struct h2x_thread* h2x_thread_new(struct h2x_options* options, void *(*start_routine)(void *));

void h2x_thread_set_epoll_fd(struct h2x_thread* thread, int epoll_fd);
void h2x_thread_set_finished_connection_channel(struct h2x_thread* thread,
                                                pthread_mutex_t* finished_connection_lock,
                                                struct h2x_connection** finished_connections);


void h2x_thread_cleanup(struct h2x_thread* thread);

int h2x_thread_add_connection(struct h2x_thread* thread, int fd);
int h2x_thread_poll_new_connections(struct h2x_thread* thread, struct h2x_socket** new_connections);

#endif //H2X_THREAD_H
