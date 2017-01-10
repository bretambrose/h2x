
#ifndef H2X_CONNECTION_H
#define H2X_CONNECTION_H

#include <stdint.h>
#include <h2x_frame.h>
#include <h2x_options.h>
#include <h2x_hash_table.h>
#include <h2x_headers.h>

//per rfc7540 section 4.2
#define MAX_RECV_FRAME_SIZE 0x4000

enum h2x_connection_state {
    NOT_ON_FRAME,
    ON_HEADER,
    HEADER_FILLED,
    ON_DATA
};

enum H2X_STREAM_ERROR;

struct h2x_connection {
    int fd;
    struct h2x_hash_table streams;
    h2x_mode mode;
    uint32_t next_outgoing_stream_id;
    uint32_t current_frame_size;
    uint32_t current_frame_read;
    struct h2x_frame* current_frame;
    struct h2x_frame_list outgoing_frames;
    enum h2x_connection_state state;

    void* user_data;
    void(*on_stream_headers_received)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t stream_id, void*);
    void(*on_stream_body_received)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool lastFrame, void*);
    void(*on_stream_error)(struct h2x_connection*, enum H2X_STREAM_ERROR, uint32_t, void*);
};

struct h2x_connection_node {
    struct h2x_connection* connection;
    struct h2x_connection_node* next;
};

void h2x_connection_init(struct h2x_connection* connection, int fd, h2x_mode mode);
void h2x_connection_cleanup(struct h2x_connection *connection);
void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t* data, uint32_t data_length);

void h2x_connection_set_stream_headers_receieved_callback(struct h2x_connection* connection,
                                                          void(*callback)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t, void*));
void h2x_connection_set_stream_body_receieved_callback(struct h2x_connection* connection,
                                                       void(*callback)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool finalFrame, void*));
void h2x_connection_set_stream_error_callback(struct h2x_connection* connection,
                                              void(*callback)(struct h2x_connection*, enum H2X_STREAM_ERROR, uint32_t, void*));

void h2x_connection_set_user_data(struct h2x_connection* connection, void*);

uint32_t h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame* frame);
uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection);

struct h2x_frame* h2x_connection_pop_frame(struct h2x_connection* connection);

#endif // H2X_CONNECTION_H
