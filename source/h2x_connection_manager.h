
#ifndef H2X_CONNECTION_MANAGER_H
#define H2X_CONNECTION_MANAGER_H

#include <pthread.h>
#include <stdint.h>

struct h2x_connection;
struct h2x_thread_node;
struct h2x_options;

struct h2x_connection_manager {
    struct h2x_options* options;
    struct h2x_thread_node *processing_threads;

    pthread_mutex_t finished_connection_lock;
    struct h2x_connection* finished_connections;
    uint32_t next_thread_id;

    uint32_t *connection_counts;
};

int h2x_connection_manager_init(struct h2x_options *options, struct h2x_connection_manager* connection_manager);
int h2x_connection_manager_cleanup(struct h2x_connection_manager* connection_manager);

struct h2x_connection* h2x_connection_manager_add_connection(struct h2x_connection_manager* connection_manager, int fd);
struct h2x_connection* h2x_connection_manager_add_client_connection(struct h2x_connection_manager* connection_manager, char* address_string, int port);

void h2x_connection_manager_pump_closed_connections(struct h2x_connection_manager* manager);

#endif // H2X_CONNECTION_MANAGER_H
