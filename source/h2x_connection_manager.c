#include <h2x_connection_manager.h>

#include <h2x_net_shared.h>
#include <h2x_options.h>
#include <h2x_thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void h2x_connection_manager_init(struct h2x_options *options, struct h2x_connection_manager* connection_manager)
{
    int i;
    struct h2x_thread_node** thread_node = &connection_manager->processing_threads;

    for(i = 0; i < options->threads; ++i)
    {
        struct h2x_thread* thread = h2x_thread_new(options, h2x_processing_thread_function);

        *thread_node = malloc(sizeof(struct h2x_thread_node));
        (*thread_node)->thread = thread;
        (*thread_node)->next = NULL;

        thread_node = &((*thread_node)->next);
    }
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

    return 0;
}

void h2x_connection_manager_add_connection(struct h2x_connection_manager* connection_manager, int fd)
{
    struct h2x_thread* add_thread = NULL;
    uint32_t lowest_count = 0;
    struct h2x_thread_node *thread_node = connection_manager->processing_threads;
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

