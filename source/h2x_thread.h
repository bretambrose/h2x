
#ifndef H2X_THREAD_H
#define H2X_THREAD_H

#include <pthread.h>
#include <stdbool.h>

struct h2x_socket {
    struct h2x_socket* next;
    int fd;
};

struct h2x_thread {
    struct h2x_thread* next;
    pthread_t thread;
    pthread_mutex_t state_lock;
    struct h2x_socket* new_connections;
    bool should_quit;
};

struct h2x_thread* h2x_thread_new(void *(*start_routine)(void *));
int h2x_thread_shutdown(struct h2x_thread* thread_chain);

int h2x_thread_add_connection(struct h2x_thread* thread, int fd);
int h2x_thread_pull_connections(struct h2x_thread* thread, struct h2x_socket** new_connections);

#endif //H2X_THREAD_H
