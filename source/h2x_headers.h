#ifndef H2X_HEADERS_H
#define H2X_HEADERS_H

struct h2x_header
{
    char* name;
    char* value;
};

struct h2x_header_list_node
{
    struct h2x_header* header;
    struct h2x_header_list_node* next;
};

struct h2x_header_list
{
    struct h2x_header_list_node* head;
    struct h2x_header_list_node* tail;
    struct h2x_header_list_node* cur;
};

void h2x_header_list_init(struct h2x_header_list* list);
void h2x_header_list_cleanup(struct h2x_header_list* list);
void h2x_header_list_append(struct h2x_header_list* list, struct h2x_header* header);
struct h2x_header* h2x_header_next(struct h2x_header_list* list);
void h2x_header_reset_iter(struct h2x_header_list* list);

#endif