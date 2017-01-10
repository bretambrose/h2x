
#include <h2x_headers.h>
#include <stddef.h>
#include <malloc.h>

void h2x_header_list_init(struct h2x_header_list* list)
{
    list->head = list->cur = list->tail = NULL;
}

void h2x_header_list_cleanup(struct h2x_header_list* list)
{
    struct h2x_header_list_node* iter = list->head;

    while(iter)
    {
        struct h2x_header_list_node* next = iter->next;
        if(iter->header && iter->header->name)
        {
            free(iter->header->name);
        }
        if(iter->header && iter->header->value)
        {
            free(iter->header->value);
        }
        if(iter->header)
        {
            free(iter->header);
        }

        free(iter);
        iter = next;
    }
}

void h2x_header_list_append(struct h2x_header_list* list, struct h2x_header* header)
{
    struct h2x_header_list_node* new_node = (struct h2x_header_list_node*)malloc(sizeof(struct h2x_header_list_node));
    new_node->next = NULL;
    new_node->header = header;

    if(!list->head)
    {
        list->head = list->cur = list->tail = new_node;
    }
    else
    {
        list->tail->next = new_node;
        list->tail = new_node;
    }
}

struct h2x_header* h2x_header_next(struct h2x_header_list* list)
{
    struct h2x_header_list_node* temp = list->cur;
    list->cur = list->cur->next;
    return temp->header;
}

void h2x_header_reset_iter(struct h2x_header_list* list)
{
    list->cur = list->head;
}
