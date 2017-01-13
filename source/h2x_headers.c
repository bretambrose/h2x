
#include <h2x_headers.h>
#include <stddef.h>
#include <malloc.h>

void h2x_header_init(struct h2x_header* header, char* name, char* value)
{
    header->name = name;
    header->value = value;
}

void h2x_header_cleanup(struct h2x_header* header)
{
    free(header->name);
    free(header->value);
}

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
        h2x_header_cleanup(&iter->header);

        free(iter);
        iter = next;
    }
}

void h2x_header_list_append(struct h2x_header_list* list, struct h2x_header header)
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
    if(list->cur)
    {
        list->cur = list->cur->next;
        return &temp->header;
    }
    return NULL;
}

void h2x_header_reset_iter(struct h2x_header_list* list)
{
    list->cur = list->head;
}
