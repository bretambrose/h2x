
#ifndef H2X_CONNECTION_H
#define H2X_CONNECTION_H

#include <stdint.h>
#include <h2x_enum_types.h>
#include <h2x_frame.h>
#include <h2x_hash_table.h>

//per rfc7540 section 4.2
#define MAX_RECV_FRAME_SIZE 0x4000

struct h2x_header_list;
struct h2x_thread;

struct h2x_connection {
    struct h2x_thread* owner;
    int fd;
    struct h2x_hash_table streams;
    h2x_mode mode;
    uint32_t next_outgoing_stream_id;
    uint32_t current_frame_size;
    uint32_t current_frame_read;
    struct h2x_frame* current_frame;
    struct h2x_frame_list outgoing_frames;
    h2x_connection_state state;

    struct h2x_frame* current_outbound_frame;
    uint32_t current_outbound_frame_read_position;

    /*
     Intrusive lists that chain together connections that require read and/or write work.
     We build and interate these chains starting from the epoll_wait return values,
     removing entries as necessary during processing (on EAGAIN or nothing left to do),
     until no more work remains to be done in either list.
     Processing reads can trigger adds to the write chain, but processing writes
     cannot trigger adds to either the read or write chain.
     */
    struct h2x_connection* pending_read_chain;
    struct h2x_connection* pending_write_chain;
    struct h2x_connection* pending_close_chain;

    void* user_data;
    void(*on_stream_headers_received)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t stream_id, void*);
    void(*on_stream_body_received)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool lastFrame, void*);
    void(*on_stream_error)(struct h2x_connection*, h2x_stream_error, uint32_t, void*);
};

void h2x_connection_init(struct h2x_connection* connection, struct h2x_thread* owner, int fd, h2x_mode mode);
void h2x_connection_cleanup(struct h2x_connection *connection);
void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t* data, uint32_t data_length);

void h2x_connection_set_stream_headers_receieved_callback(struct h2x_connection* connection,
                                                          void(*callback)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t, void*));
void h2x_connection_set_stream_body_receieved_callback(struct h2x_connection* connection,
                                                       void(*callback)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool finalFrame, void*));
void h2x_connection_set_stream_error_callback(struct h2x_connection* connection,
                                              void(*callback)(struct h2x_connection*, h2x_stream_error, uint32_t, void*));

void h2x_connection_set_user_data(struct h2x_connection* connection, void*);

void h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame* frame);
uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection);

struct h2x_frame* h2x_connection_pop_frame(struct h2x_connection* connection);
bool h2x_connection_write_outbound_data(struct h2x_connection* connection);
void h2x_connection_on_new_outbound_data(struct h2x_connection* connection);

#endif // H2X_CONNECTION_H
