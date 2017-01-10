
#ifndef H2X_CONNECTION_MANAGER_H
#define H2X_CONNECTION_MANAGER_H

struct h2x_thread_node;
struct h2x_options;

struct h2x_connection_manager {
    struct h2x_thread_node *processing_threads;
};

void h2x_connection_manager_init(struct h2x_options *options, struct h2x_connection_manager* connection_manager);
int h2x_connection_manager_cleanup(struct h2x_connection_manager* connection_manager);

void h2x_connection_manager_add_connection(struct h2x_connection_manager* connection_manager, int fd);

#endif // H2X_CONNECTION_MANAGER_H
