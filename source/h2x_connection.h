
#ifndef H2X_CONNECTION_H
#define H2X_CONNECTION_H

#include <stdint.h>
#include <h2x_enum_types.h>
#include <h2x_frame.h>
#include <h2x_hash_table.h>
#include <h2x_headers.h>
#include <h2x_request.h>

//per rfc7540 section 4.2
#define MAX_RECV_FRAME_SIZE 0x4000

struct h2x_header_list;
struct h2x_stream;
struct h2x_thread;

struct h2x_socket_state {
    uint64_t bytes_written;
    uint64_t bytes_read;
    int io_error;
    uint32_t last_event_mask;
    bool has_connected;
    bool has_remote_hungup;
};

void h2x_socket_state_init(struct h2x_socket_state* socket_state);

struct h2x_connection {
    struct h2x_thread* owner;
    h2x_connection_state state;
    int fd;
    struct h2x_request* queued_request;

    struct h2x_socket_state socket_state;
    /*
     Intrusive lists that chain together connections that require read and/or write work.
     We build and interate these chains starting from the epoll_wait return values,
     removing entries as necessary during processing (on EAGAIN or nothing left to do),
     until no more work remains to be done in either list.
     Processing reads can trigger adds to the write chain, but processing writes
     cannot trigger adds to either the read or write chain.
     */
    struct h2x_connection* intrusive_chains[H2X_ICT_COUNT];
    /*
     * There has to be a better way of doing this but I can't figure out how to detect
     * in-chain when you're the last element in the list without either adding a bool
     * or making the chain doubly-linked.  We don't actually need the back-tracking
     * ability of a doubly-linked list, so just use bool markers.
     * If we ever need to remove a connection from a chain outside of
     * the process iteration, then we'll need to go doubly-linked.
     * One way we might end up doing that is if we try to be more responsive
     * with closed connections
     */
    bool in_intrusive_chain[H2X_ICT_COUNT];

    struct h2x_hash_table streams;
    uint32_t next_outgoing_stream_id;
    uint32_t current_frame_size;
    uint32_t current_frame_read;
    struct h2x_frame* current_frame;
    struct h2x_frame_list outgoing_frames;
    h2x_read_frame_state read_frame_state;

    struct h2x_frame* current_outbound_frame;
    uint32_t current_outbound_frame_read_position;

    uint32_t last_seen_stream_id;
    h2x_frame_type last_seen_frame_type;

    void* user_data;
    void(*on_stream_headers_received)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t stream_id, void*);
    void(*on_stream_body_received)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool lastFrame, void*);
    void(*on_stream_error)(struct h2x_connection*, h2x_connection_error, uint32_t, void*);
    bool(*on_stream_data_needed)(struct h2x_connection*, uint32_t, uint8_t*, uint32_t, uint32_t*, void*);

};

void h2x_connection_init(struct h2x_connection* connection, struct h2x_thread* owner, int fd);
void h2x_connection_cleanup(struct h2x_connection *connection);
void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t* data, uint32_t data_length);

void h2x_connection_set_stream_headers_receieved_callback(struct h2x_connection* connection,
                                                          void(*callback)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t, void*));
void h2x_connection_set_stream_body_receieved_callback(struct h2x_connection* connection,
                                                       void(*callback)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool finalFrame, void*));
void h2x_connection_set_stream_data_needed_callback(struct h2x_connection* connection,
                                              bool(*on_stream_data_needed)(struct h2x_connection*, uint32_t, uint8_t*, uint32_t, uint32_t*, void*));

void h2x_connection_set_stream_error_callback(struct h2x_connection* connection,
                                              void(*callback)(struct h2x_connection*, h2x_connection_error, uint32_t, void*));

void h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame* frame, h2x_stream_push_dir stream_push_dir);

void h2x_push_headers(struct h2x_connection* connection, uint32_t stream_id, struct h2x_header_list*);
void h2x_push_data_segment(struct h2x_connection* connection, uint32_t stream_id, uint8_t* data, uint32_t size, bool lastFrame);
void h2x_push_rst_stream(struct h2x_connection* connection, uint32_t stream_id, h2x_connection_error error);
uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection, void* user_data);

struct h2x_frame* h2x_connection_pop_frame(struct h2x_connection* connection);
void h2x_connection_on_new_outbound_data(struct h2x_connection* connection);

void h2x_connection_add_to_intrusive_chain(struct h2x_connection* connection, h2x_intrusive_chain_type chain);
void h2x_connection_remove_from_intrusive_chain(struct h2x_connection** connection_ref, h2x_intrusive_chain_type chain);

void h2x_connection_pump_outbound_frame(struct h2x_connection* connection);

void h2x_connection_process_inbound_frame(struct h2x_connection* connection, struct h2x_frame* frame);
void h2x_connection_process_outbound_frame(struct h2x_connection* connection, struct h2x_frame* frame);

void h2x_connection_add_request(struct h2x_connection* connection, struct h2x_request* request);

h2x_connection_error h2x_connection_handle_inbound_push_promise(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_header(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_continuation(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_stream_closed(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_stream_data(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_stream_window_update(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_stream_priority(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream);
h2x_connection_error h2x_connection_handle_inbound_stream_error(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream, h2x_connection_error);

void h2x_connection_begin_close(struct h2x_connection* connection);

#endif // H2X_CONNECTION_H
