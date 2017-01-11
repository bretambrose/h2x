#include <h2x_connection_manager.h>

#include <h2x_connection.h>
#include <h2x_net_shared.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int h2x_connection_manager_init(struct h2x_options *options, struct h2x_connection_manager* connection_manager)
{
    connection_manager->options = options;
    connection_manager->finished_connections = NULL;

    if(!pthread_mutex_init(&connection_manager->finished_connection_lock, NULL))
    {
        return -1;
    }

    uint32_t i;
    struct h2x_thread_node** thread_node = &connection_manager->processing_threads;

    for(i = 0; i < options->threads; ++i)
    {
        struct h2x_thread* thread = h2x_thread_new(options, h2x_processing_thread_function);
        h2x_thread_set_finished_connection_channel(thread, &connection_manager->finished_connection_lock, &connection_manager->finished_connections);

        *thread_node = malloc(sizeof(struct h2x_thread_node));
        (*thread_node)->thread = thread;
        (*thread_node)->next = NULL;

        thread_node = &((*thread_node)->next);
    }

    return 0;
}

int h2x_connection_manager_cleanup(struct h2x_connection_manager* connection_manager)
{
    if(!connection_manager)
    {
        return 0;
    }

    /* tell everyone to quit */
    struct h2x_thread_node* thread_node = connection_manager->processing_threads;
    while(thread_node)
    {
        struct h2x_thread* thread = thread_node->thread;

        pthread_mutex_lock(&thread->state_lock);
        thread->should_quit = true;
        pthread_mutex_unlock(&thread->state_lock);

        thread_node = thread_node->next;
    }

    /* wait for everyone to quit */
    thread_node = connection_manager->processing_threads;
    while(thread_node)
    {
        struct h2x_thread* thread = thread_node->thread;

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

    /* cleanup all finished connections */
    /* don't need mutex because all other threads have exited at this point */
    struct h2x_connection* connection = connection_manager->finished_connections;
    while(connection)
    {
        struct h2x_connection* next_connection = connection->intrusive_chains[H2X_ICT_PENDING_CLOSE];
        int fd = connection->fd;

        h2x_connection_cleanup(connection);
        close(fd);
        free(connection);

        connection = next_connection;
    }

    connection_manager->finished_connections = NULL;

    pthread_mutex_destroy(&connection_manager->finished_connection_lock);

    return 0;
}

void h2x_connection_manager_add_connection(struct h2x_connection_manager* connection_manager, int fd)
{
    struct h2x_thread* add_thread = NULL;
    uint32_t lowest_count = 0;
    struct h2x_thread_node* thread_node = connection_manager->processing_threads;
    while(thread_node)
    {
        uint32_t thread_connection_count = thread_node->thread->connection_count;
        if(thread_connection_count <= lowest_count)
        {
            add_thread = thread_node->thread;
            lowest_count = thread_connection_count;
        }

        thread_node = thread_node->next;
    }

    if(!add_thread || h2x_thread_add_connection(add_thread, fd))
    {
        fprintf(stderr, "Something went very wrong in h2x_connection_manager_add_connection\n");
        close(fd);
    }
}

void h2x_connection_manager_pump_closed_connections(struct h2x_connection_manager* manager)
{
    struct h2x_connection* finished_connections = NULL;

    if(!pthread_mutex_lock(&manager->finished_connection_lock))
    {
        fprintf(stderr, "Unable to lock connection manager finished connections\n");
        return;
    }

    finished_connections = manager->finished_connections;
    manager->finished_connections = NULL;

    pthread_mutex_unlock(&manager->finished_connection_lock);

    struct h2x_connection* connection = finished_connections;
    while(connection)
    {
        struct h2x_connection *next_connection = connection->intrusive_chains[H2X_ICT_PENDING_CLOSE];

        h2x_connection_cleanup(connection);
        close(connection->fd);
        free(connection);

        connection = next_connection;
    }
}

