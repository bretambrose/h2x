
#include <h2x_stream.h>
#include <h2x_connection.h>
#include <h2x_request.h>
#include <memory.h>

void h2x_request_init(struct h2x_request* request, struct h2x_connection* connection, void* user_data)
{
    h2x_header_list_init(&request->header_list);
    request->connection = connection;
    request->user_data = user_data;
    request->next = NULL;
    request->stream_id = 0;
}

void h2x_request_cleanup(struct h2x_request* request)
{
    h2x_header_list_cleanup(&request->header_list);
    request->connection = NULL;
    request->user_data = NULL;
}

void h2x_headers_add(struct h2x_request* request, char* name, char* value)
{
    struct h2x_header header;
    h2x_header_init(&header, name, value);
    h2x_header_list_append(&request->header_list, header);
}

void h2x_headers_set_user_data(struct h2x_request* request, void* user_data)
{
    request->user_data = user_data;
}
