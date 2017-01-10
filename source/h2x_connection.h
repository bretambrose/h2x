
#ifndef H2X_CONNECTION_H
#define H2X_CONNECTION_H

#include <stdint.h>
#include <h2x_stream.h>
#include <h2x_options.h>
#include <h2x_hash_table.h>

//per rfc7540 section 4.2
#define MAX_RECV_FRAME_SIZE 0x4000

enum h2x_connection_state {
    NOT_ON_FRAME,
    ON_HEADER,
    HEADER_FILLED,
    ON_DATA
};

struct h2x_connection {
    int fd;
    struct h2x_hash_table streams;
    h2x_mode mode;
    uint32_t next_outgoing_stream_id;
    uint32_t current_frame_size;
    uint32_t current_frame_read;
    struct h2x_frame* current_frame;
    enum h2x_connection_state state;
};

struct h2x_connection_node {
    struct h2x_connection* connection;
    struct h2x_connection_node* next;
};

void h2x_connection_init(struct h2x_connection* connection, int fd, h2x_mode mode);
void h2x_connection_cleanup(struct h2x_connection *connection);
void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t* data, uint32_t data_length);

uint32_t h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame* frame);
uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection);

#endif // H2X_CONNECTION_H
