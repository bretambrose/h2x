#ifndef H2X_REQUEST_H
#define H2X_REQUEST_H

#include <h2x_headers.h>

struct h2x_request {
    struct h2x_connection* connection;
    void* user_data;
    struct h2x_header_list header_list;
    struct h2x_request* next;

    uint32_t stream_id;
};

void h2x_request_init(struct h2x_request* request, struct h2x_connection* connection, void* user_data);

void h2x_request_cleanup(struct h2x_request* request);

void h2x_headers_add(struct h2x_request* request, char* name, char* value);

void h2x_headers_set_user_data(struct h2x_request* request, void*);

#endif /* H2X_REQUEST_H*/