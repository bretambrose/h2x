#include <h2x_stream.h>
#include <h2x_connection.h>
#include <h2x_log.h>
#include <h2x_thread.h>

#include <assert.h>
#include <memory.h>
#include <stdlib.h>

void h2x_socket_state_init(struct h2x_socket_state* socket_state)
{
    socket_state->io_error = 0;
    socket_state->last_event_mask = 0;
    socket_state->has_connected = false;
    socket_state->has_remote_hungup = false;
}


#define min(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

static uint32_t stream_hash_function(void *arg) {
    struct h2x_stream *stream = (struct h2x_stream *) arg;
    return stream->stream_identifier;
}

static void get_padding(struct h2x_frame *frame, uint8_t *padding_offset, uint8_t *padding_length) {
    *padding_offset = 0;
    *padding_length = 0;

    if (h2x_frame_get_flags(frame) & H2X_PADDED) {
        *padding_offset = 1;
        *padding_length = h2x_frame_get_payload(frame)[0];
    }
}

void stream_headers_received(struct h2x_stream *stream, struct h2x_frame *frame, void *data) {
    struct h2x_connection *connection = (struct h2x_connection *) data;

    if (connection->on_stream_headers_received) {
        uint8_t padding_offset = 0;
        uint8_t padding_length = 0;

        get_padding(frame, &padding_offset, &padding_length);

        struct h2x_header_list *headers = NULL;//decode and aggregate these.
        connection->on_stream_headers_received(connection, headers,
                                               stream->stream_identifier, connection->user_data);
    }
}

void stream_data_received(struct h2x_stream *stream, struct h2x_frame *frame, bool final_frame, void *data) {
    struct h2x_connection *connection = (struct h2x_connection *) data;

    if (connection->on_stream_body_received) {
        uint8_t padding_offset = 0;
        uint8_t padding_length = 0;

        get_padding(frame, &padding_offset, &padding_length);

        connection->on_stream_body_received(connection, h2x_frame_get_payload(frame) + padding_offset,
                                            h2x_frame_get_length(frame) - padding_length,
                                            stream->stream_identifier, final_frame, connection->user_data);
    }
}

void stream_error(struct h2x_stream *stream, struct h2x_frame *frame, h2x_connection_error error, void *data) {
    struct h2x_connection *connection = (struct h2x_connection *) data;

    if (connection->on_stream_error) {
        connection->on_stream_error(connection, error, stream->stream_identifier, connection->user_data);
    }
}

void h2x_connection_init(struct h2x_connection *connection, struct h2x_thread *owner, int fd, h2x_mode mode) {
    connection->owner = owner;
    connection->fd = fd;
    h2x_socket_state_init(&connection->socket_state);
    connection->mode = mode;
    connection->current_frame_size = 0;
    connection->state = H2X_NOT_ON_FRAME;
    connection->current_outbound_frame = NULL;
    connection->current_outbound_frame_read_position = 0;
    connection->bytes_written = 0;
    connection->bytes_read = 0;
    connection->next_new_connection = NULL;

    for (uint32_t i = 0; i < H2X_ICT_COUNT; ++i) {
        connection->intrusive_chains[i] = NULL;
        connection->in_intrusive_chain[i] = false;
    }

    connection->on_stream_headers_received = NULL;
    connection->on_stream_body_received = NULL;
    connection->on_stream_error = NULL;
    connection->on_stream_data_needed = NULL;

    connection->user_data = NULL;
    h2x_frame_list_init(&connection->outgoing_frames);

    if (mode == H2X_MODE_SERVER) {
        connection->next_outgoing_stream_id = 2;
    } else {
        connection->next_outgoing_stream_id = 1;
    }

    h2x_hash_table_init(&connection->streams, 50, &stream_hash_function);
}

void h2x_connection_cleanup(struct h2x_connection *connection) {
    h2x_hash_table_cleanup(&connection->streams);
}

void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t *data, uint32_t data_length) {
    uint32_t read = 0;
    uint32_t amount_to_read = 0;

    do {
        switch (connection->state) {
            case H2X_NOT_ON_FRAME:
                connection->current_frame_read = 0;
                connection->current_frame_size = 0;
                connection->current_frame = (struct h2x_frame *) malloc(sizeof(struct h2x_frame));
                h2x_frame_init(connection->current_frame);

                connection->current_frame->raw_data = (uint8_t *) malloc(MAX_RECV_FRAME_SIZE);
                connection->current_frame->size = MAX_RECV_FRAME_SIZE;
                connection->state = H2X_ON_HEADER;
                break;

            case H2X_ON_HEADER:
                amount_to_read = min(data_length - read, FRAME_HEADER_LENGTH - connection->current_frame_read);
                memcpy(connection->current_frame->raw_data + connection->current_frame_read, data + read,
                       amount_to_read);
                connection->current_frame_read += amount_to_read;
                read += amount_to_read;

                if (amount_to_read >= FRAME_HEADER_LENGTH) {
                    connection->state = H2X_HEADER_FILLED;
                }
                break;

            case H2X_HEADER_FILLED:
                connection->current_frame_size = h2x_frame_get_length(connection->current_frame) + FRAME_HEADER_LENGTH;
                connection->state = H2X_ON_DATA;
                break;

            case H2X_ON_DATA:
                amount_to_read = min(data_length - read, connection->current_frame_size - FRAME_HEADER_LENGTH -
                                                         connection->current_frame_read);
                memcpy(connection->current_frame->raw_data + connection->current_frame_read, data + read,
                       amount_to_read);
                read += amount_to_read;
                connection->current_frame_read += amount_to_read;

                if (connection->current_frame_read == connection->current_frame_size) {
                    h2x_connection_push_frame_to_stream(connection, connection->current_frame, H2X_STREAM_INBOUND);
                    connection->state = H2X_NOT_ON_FRAME;
                }
        }
    } while (read < data_length);
}

void h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame *frame, h2x_stream_push_dir push_dir) {
    struct h2x_stream *stream = h2x_hash_table_find(&connection->streams, h2x_frame_get_stream_identifier(frame));

    if (!stream) {
        stream = (struct h2x_stream *) malloc(sizeof(struct h2x_stream));
        h2x_stream_init(stream);
        stream->stream_identifier = h2x_frame_get_stream_identifier(frame);
        h2x_hash_table_add(&connection->streams, stream);
    }

    if (push_dir == H2X_STREAM_INBOUND) {
        h2x_connection_process_inbound_frame(connection, frame);
    } else {
        h2x_connection_process_outbound_frame(connection, frame);
    }
}

uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection, void* user_data) {
    uint32_t stream_id = connection->next_outgoing_stream_id;
    connection->next_outgoing_stream_id += 2;

    struct h2x_stream *stream = (struct h2x_stream *) malloc(sizeof(struct h2x_stream));
    h2x_stream_init(stream);
    stream->stream_identifier = stream_id;
    stream->user_data = user_data;
    h2x_hash_table_add(&connection->streams, stream);

    return stream_id;
}

void h2x_push_headers(struct h2x_connection* connection, uint32_t stream_id, struct h2x_header_list* header_list)
{
    struct h2x_frame* frame = (struct h2x_frame*)malloc(sizeof(struct h2x_frame));
    h2x_frame_init(frame);
    frame->raw_data = (uint8_t *) malloc(MAX_RECV_FRAME_SIZE);
    frame->size = MAX_RECV_FRAME_SIZE;
    h2x_frame_set_type(frame, H2X_HEADERS);
    h2x_frame_set_stream_identifier(frame, stream_id);
    h2x_frame_set_length(frame, MAX_RECV_FRAME_SIZE - FRAME_HEADER_LENGTH);
    uint32_t headers_written_size = 0;
    uint32_t size_of_eq = 1;
    uint32_t size_of_end = 2;

    struct h2x_header* cur = NULL;

    while((cur = h2x_header_next(header_list)))
    {
        char* name = cur->name;
        char* value = cur->value;

        size_t name_len = strlen(name);
        size_t value_len = strlen(value);

        if(name_len + value_len + size_of_eq + size_of_end + headers_written_size > h2x_frame_get_length(frame) )
        {
            h2x_frame_set_length(frame, headers_written_size);
            frame->size = headers_written_size + FRAME_HEADER_LENGTH;
            h2x_connection_push_frame_to_stream(connection, frame, H2X_STREAM_INBOUND);
            headers_written_size = 0;
            frame = (struct h2x_frame*)malloc(sizeof(struct h2x_frame));
            h2x_frame_init(frame);
            frame->raw_data = (uint8_t *) malloc(MAX_RECV_FRAME_SIZE);
            frame->size = MAX_RECV_FRAME_SIZE;
            h2x_frame_set_type(frame, H2X_CONTINUATION);
            h2x_frame_set_stream_identifier(frame, stream_id);
            h2x_frame_set_length(frame, MAX_RECV_FRAME_SIZE - FRAME_HEADER_LENGTH);
        }

        memcpy(frame->raw_data + FRAME_HEADER_LENGTH + headers_written_size, name, name_len);
        headers_written_size += name_len;
        memcpy(frame->raw_data + FRAME_HEADER_LENGTH + headers_written_size, "=", size_of_eq);
        headers_written_size += size_of_eq;
        memcpy(frame->raw_data + FRAME_HEADER_LENGTH + headers_written_size, value, value_len);
        headers_written_size += value_len;
        memcpy(frame->raw_data + FRAME_HEADER_LENGTH + headers_written_size, "\r\n", size_of_end);
        headers_written_size += size_of_end;
    }

    h2x_frame_set_length(frame, headers_written_size);
    h2x_frame_set_flags(frame, H2X_END_HEADERS);
    frame->size = headers_written_size + FRAME_HEADER_LENGTH;

    h2x_connection_push_frame_to_stream(connection, frame, H2X_STREAM_INBOUND);
}

void h2x_push_data_segment(struct h2x_connection* connection, uint32_t stream_id, uint8_t* data, uint32_t size, bool lastFrame)
{
    struct h2x_frame* frame = (struct h2x_frame*)malloc(sizeof(struct h2x_frame));
    h2x_frame_init(frame);
    frame->raw_data = (uint8_t *) malloc(MAX_RECV_FRAME_SIZE);
    frame->size = MAX_RECV_FRAME_SIZE;
    h2x_frame_set_stream_identifier(frame, stream_id);
    h2x_frame_set_type(frame, H2X_DATA);

    uint32_t data_written_size = 0;

    while(data_written_size < size)
    {
        uint32_t to_write = min(size, (uint32_t)(MAX_RECV_FRAME_SIZE - FRAME_HEADER_LENGTH));
        memcpy(frame->raw_data + FRAME_HEADER_LENGTH, data, to_write);
        h2x_frame_set_length(frame, to_write);
        frame->size = to_write + FRAME_HEADER_LENGTH;

        data_written_size += to_write;

        if(data_written_size == size && lastFrame)
        {
            h2x_frame_set_flags(frame, H2X_END_STREAM);
        }

        h2x_connection_push_frame_to_stream(connection, frame, H2X_STREAM_OUTBOUND);

        if(data_written_size < size)
        {
            frame = (struct h2x_frame*) malloc(sizeof(struct h2x_frame));
            h2x_frame_init(frame);
            frame->raw_data = (uint8_t *) malloc(MAX_RECV_FRAME_SIZE);
            frame->size = MAX_RECV_FRAME_SIZE;
            h2x_frame_set_type(frame, H2X_DATA);
            h2x_frame_set_stream_identifier(frame, stream_id);
        }
    }
}

struct h2x_frame *h2x_connection_pop_frame(struct h2x_connection *connection) {
    return h2x_frame_list_pop(&connection->outgoing_frames);
}

void h2x_connection_set_stream_headers_receieved_callback(struct h2x_connection *connection,
                                                          void(*callback)(struct h2x_connection *,
                                                                          struct h2x_header_list *headers, uint32_t,
                                                                          void *)) {
    connection->on_stream_headers_received = callback;
}

void h2x_connection_set_stream_body_receieved_callback(struct h2x_connection *connection,
                                                       void(*callback)(struct h2x_connection *, uint8_t *data,
                                                                       uint32_t length, uint32_t, bool finalFrame,
                                                                       void *)) {
    connection->on_stream_body_received = callback;
}

void h2x_connection_set_stream_data_needed_callback(struct h2x_connection* connection,
                                                    bool(*on_stream_data_needed)(struct h2x_connection*, uint32_t, uint8_t*, uint32_t, uint32_t*, void*))
{
    connection->on_stream_data_needed = on_stream_data_needed;
}

void h2x_connection_set_stream_error_callback(struct h2x_connection *connection,
                                              void(*callback)(struct h2x_connection *, h2x_connection_error, uint32_t,
                                                              void *)) {
    connection->on_stream_error = callback;
}

void h2x_connection_set_user_data(struct h2x_connection *connection, void *user_data) {
    connection->user_data = user_data;
}

static bool h2x_connection_is_in_intrusive_chain(struct h2x_connection *connection, h2x_intrusive_chain_type chain) {
    return connection->in_intrusive_chain[chain];
}

void h2x_connection_add_to_intrusive_chain(struct h2x_connection *connection, h2x_intrusive_chain_type chain) {
    if (h2x_connection_is_in_intrusive_chain(connection, chain)) {
        return;
    }

    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Adding connection %d to chain %d\n", connection->fd, (int) chain);

    struct h2x_thread* thread = connection->owner;

    assert(connection->intrusive_chains[chain] == NULL);
    assert(!connection->in_intrusive_chain[chain]);

    connection->in_intrusive_chain[chain] = true;

    if (thread->intrusive_chains[chain] == NULL) {
        thread->intrusive_chains[chain] = connection;
        return;
    }

    connection->intrusive_chains[chain] = thread->intrusive_chains[chain];
    thread->intrusive_chains[chain] = connection->intrusive_chains[chain];
}

void h2x_connection_remove_from_intrusive_chain(struct h2x_connection** connection_ref, h2x_intrusive_chain_type chain)
{
    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "Removing connection %d from chain %d\n", (*connection_ref)->fd, (int) chain);

    struct h2x_connection* connection = *connection_ref;

    assert(connection->in_intrusive_chain[chain]);

    *connection_ref = (*connection_ref)->intrusive_chains[chain];

    connection->in_intrusive_chain[chain] = false;
    connection->intrusive_chains[chain] = NULL;
}

void h2x_connection_on_new_outbound_data(struct h2x_connection* connection)
{
    h2x_connection_add_to_intrusive_chain(connection, H2X_ICT_PENDING_WRITE);
}

void h2x_connection_pump_outbound_frame(struct h2x_connection *connection) {
    if (connection->current_outbound_frame &&
        connection->current_outbound_frame_read_position >= connection->current_outbound_frame->size) {
        h2x_frame_cleanup(connection->current_outbound_frame);
        free(connection->current_outbound_frame);

        connection->current_outbound_frame = NULL;
    }

    if (!connection->current_outbound_frame) {
        connection->current_outbound_frame = h2x_connection_pop_frame(connection);
        connection->current_outbound_frame_read_position = 0;
    }
}

void h2x_connection_process_inbound_frame(struct h2x_connection* connection, struct h2x_frame* frame) {
    h2x_frame_type frame_type = h2x_frame_get_type(frame);
    uint32_t stream_id = h2x_frame_get_stream_identifier(frame);
    uint8_t frame_flags = h2x_frame_get_flags(frame);
    h2x_connection_error error = H2X_NO_ERROR;

    struct h2x_stream* stream = h2x_hash_table_find(&connection->streams, stream_id);
    assert(stream);

    h2x_stream_state stream_state = stream->state;
    h2x_stream_state next_state = stream_state;

    //continuation doesn't really fit into the state model. Handle it first
    //then use a normal state pattern after that.
    if (connection->last_seen_frame_type == H2X_CONTINUATION && frame_type != H2X_CONTINUATION &&
        !stream->end_header_sent) {
        error = H2X_PROTOCOL_ERROR;
    } else if (frame_type == H2X_CONTINUATION) {
        if (stream_id != connection->last_seen_stream_id || stream->end_header_sent ||
            (connection->last_seen_frame_type != H2X_HEADERS &&
             connection->last_seen_frame_type != H2X_CONTINUATION &&
             connection->last_seen_frame_type != H2X_PUSH_PROMISE)) {
            error = H2X_PROTOCOL_ERROR;
        } else {
            error = h2x_connection_handle_inbound_header(connection, frame, stream);
        }
    }

    connection->last_seen_frame_type = frame_type;
    connection->last_seen_stream_id = stream_id;

    if (!error && frame_type != H2X_CONTINUATION) {
        switch (stream_state) {
            case H2X_IDLE:
                switch (frame_type) {
                    case H2X_PUSH_PROMISE:
                        error = h2x_connection_handle_inbound_push_promise(connection, frame, stream);
                        next_state = H2X_RESERVED_REMOTE;
                        break;
                    case H2X_HEADERS:
                        error = h2x_connection_handle_inbound_header(connection, frame, stream);
                        next_state = H2X_OPEN;
                        break;
                    default:
                        error = H2X_PROTOCOL_ERROR;
                        break;
                }
                break;
            case H2X_RESERVED_REMOTE:
                switch (frame_type) {
                    case H2X_HEADERS:
                        error = h2x_connection_handle_inbound_header(connection, frame, stream);
                        next_state = H2X_HALF_CLOSED_LOCAL;
                        break;
                    case H2X_PRIORITY:
                        error = h2x_connection_handle_inbound_stream_priority(connection, frame, stream);
                        break;
                    case H2X_RST_STREAM:
                        error = h2x_connection_handle_inbound_stream_closed(connection, frame, stream);
                        next_state = H2X_CLOSED;
                        break;
                    default:
                        error = H2X_PROTOCOL_ERROR;
                        break;
                }
                break;
            case H2X_OPEN:
                switch (frame_type) {
                    case H2X_RST_STREAM:
                        error = h2x_connection_handle_inbound_stream_closed(connection, frame, stream);
                        next_state = H2X_CLOSED;
                        break;
                    case H2X_HEADERS:
                        error = h2x_connection_handle_inbound_header(connection, frame, stream);
                        break;
                    case H2X_DATA:
                        error = h2x_connection_handle_inbound_stream_data(connection, frame, stream);
                        break;
                    case H2X_WINDOW_UPDATE:
                        error = h2x_connection_handle_inbound_stream_window_update(connection, frame, stream);
                        break;
                    case H2X_PRIORITY:
                        error = h2x_connection_handle_inbound_stream_priority(connection, frame, stream);
                        break;
                    default:
                        break;
                }

                if (frame_flags & H2X_END_STREAM && error == H2X_NO_ERROR) {
                    next_state = H2X_HALF_CLOSED_REMOTE;
                }
                break;
            case H2X_HALF_CLOSED_REMOTE:
                switch (frame_type) {
                    case H2X_RST_STREAM:
                        error = h2x_connection_handle_inbound_stream_closed(connection, frame, stream);
                        next_state = H2X_CLOSED;
                        break;
                    case H2X_WINDOW_UPDATE:
                        error = h2x_connection_handle_inbound_stream_window_update(connection, frame, stream);
                        break;
                    case H2X_PRIORITY:
                        error = h2x_connection_handle_inbound_stream_priority(connection, frame, stream);
                        break;
                    default:
                        error = H2X_STREAM_CLOSED;
                        break;
                }
                break;
            case H2X_HALF_CLOSED_LOCAL:
                switch (frame_type) {
                    case H2X_RST_STREAM:
                        error = h2x_connection_handle_inbound_stream_closed(connection, frame, stream);
                        next_state = H2X_CLOSED;
                        break;
                    case H2X_WINDOW_UPDATE:
                        error = h2x_connection_handle_inbound_stream_window_update(connection, frame, stream);
                        break;
                    case H2X_PRIORITY:
                        error = h2x_connection_handle_inbound_stream_priority(connection, frame, stream);
                        break;
                    case H2X_DATA:
                        error = h2x_connection_handle_inbound_stream_data(connection, frame, stream);
                        break;
                    case H2X_HEADERS:
                        error = h2x_connection_handle_inbound_header(connection, frame, stream);
                        break;
                    default:
                        error = H2X_PROTOCOL_ERROR;
                        break;
                }
                break;
            case H2X_CLOSED:
                if (frame_type == H2X_PRIORITY) {
                    error = h2x_connection_handle_inbound_stream_priority(connection, frame, stream);
                    break;
                } else {
                    error = H2X_STREAM_CLOSED;
                }
                break;
            default:
                error = H2X_PROTOCOL_ERROR;
                break;
        }
    }

    if(!error) {
        h2x_stream_set_state(stream, next_state);
    } else {
        h2x_stream_set_state(stream, H2X_CLOSED);
        h2x_connection_handle_inbound_stream_error(connection, frame, stream, error);
    }
}

void h2x_connection_process_outbound_frame(struct h2x_connection *connection, struct h2x_frame *frame) {
    h2x_frame_type frame_type = h2x_frame_get_type(frame);
    uint32_t stream_id = h2x_frame_get_stream_identifier(frame);
    uint8_t frame_flags = h2x_frame_get_flags(frame);
    //h2x_connection_error error = H2X_NO_ERROR;

    struct h2x_stream* stream = h2x_hash_table_find(&connection->streams, stream_id);
    assert(stream);

    h2x_stream_state stream_state = stream->state;
    h2x_stream_state next_state = stream_state;
    bool valid_state = true;

    switch(stream_state) {
        case H2X_IDLE:
            switch(frame_type)
            {
                case H2X_HEADERS:
                    next_state = H2X_OPEN;
                    break;
                case H2X_PUSH_PROMISE:
                    next_state = H2X_RESERVED_LOCAL;
                    break;
                default:
                    valid_state = false;
                    break;
            }
            break;
        case H2X_RESERVED_LOCAL:
            switch(frame_type)
            {
                case H2X_HEADERS:
                    next_state = H2X_HALF_CLOSED_REMOTE;
                    break;
                case H2X_RST_STREAM:
                    next_state = H2X_CLOSED;
                    break;
                case H2X_PRIORITY:
                    break;
                default:
                    valid_state = false;
                    break;
            }
            break;
        case H2X_RESERVED_REMOTE:
            switch(frame_type) {
                case H2X_RST_STREAM:
                    next_state = H2X_CLOSED;
                    break;
                case H2X_PRIORITY:
                    break;
                default:
                    valid_state = false;
                    break;
            }
            break;
        case H2X_OPEN:
            if(frame_flags & H2X_END_STREAM) {
                next_state = H2X_HALF_CLOSED_REMOTE;
            }
            break;
        case H2X_HALF_CLOSED_LOCAL:
            switch(frame_type) {
                case H2X_RST_STREAM:
                    next_state = H2X_CLOSED;
                    break;
                case H2X_PRIORITY:
                    break;
                case H2X_WINDOW_UPDATE:
                    break;
                default:
                    valid_state = false;
                    break;
            }
            break;
        case H2X_HALF_CLOSED_REMOTE:
            switch(frame_type) {
                case H2X_RST_STREAM:
                    next_state = H2X_CLOSED;
                    break;
                default:
                    break;
            }
            break;
        case H2X_CLOSED:
            switch(frame_type) {
                case H2X_PRIORITY:
                    break;
                default:
                    valid_state = false;
                    break;
            }
            break;
        default:
            valid_state = false;
            break;
    }

    if(valid_state) {
        h2x_stream_set_state(stream, next_state);
        h2x_frame_list_append(&connection->outgoing_frames, frame);
        h2x_connection_on_new_outbound_data(connection);
    }
}

h2x_connection_error h2x_connection_handle_inbound_push_promise(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {
    return H2X_NO_ERROR;
}

void parse_header_frames_and_trigger_callback(struct h2x_connection* connection, struct h2x_stream* stream) {
    struct h2x_frame_list* header_fragments = &stream->header_fragments;
    struct h2x_header_list* header_list = (struct h2x_header_list*)malloc(sizeof(struct h2x_header_list));
    h2x_header_list_init(header_list);
    struct h2x_frame* current_frame = NULL;

    while((current_frame = h2x_frame_list_pop(header_fragments))) {

        uint32_t read_index = 0;
        uint32_t line_end_index = 0;
        uint32_t delimiter_index = 0;
        uint32_t start_mark = 0;
        uint32_t current_char = 0;
        uint8_t* payload = h2x_frame_get_payload(current_frame);
        uint32_t payload_length = h2x_frame_get_length(current_frame);
        uint8_t* payloadIter = payload;

        while(read_index < payload_length && (current_char = *payloadIter++)) {
            if(current_char == '=') {
                delimiter_index = read_index;
            }
            if(current_char == '\r') {
                line_end_index = read_index;
            }

            if(delimiter_index && line_end_index) {
                char* name = (char*)malloc((delimiter_index - start_mark / sizeof(char)) + 1);
                char* value = (char*)malloc((line_end_index - delimiter_index / sizeof(char)) + 1);

                memcpy(name, payload + start_mark, delimiter_index - start_mark);
                memcpy(value, payload + delimiter_index + 1, line_end_index - delimiter_index - 1);
                start_mark = line_end_index + 2;

                delimiter_index = 0;
                line_end_index = 0;

                struct h2x_header header;
                h2x_header_init(&header, name, value);
                h2x_header_list_append(header_list, header);
            }

            read_index++;
        }
    }

    if(connection->on_stream_headers_received) {
        connection->on_stream_headers_received(connection, header_list, stream->stream_identifier, stream->user_data);
    }
}

h2x_connection_error h2x_connection_handle_inbound_header(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {
    if(h2x_frame_get_flags(frame) & H2X_END_HEADERS) {
        h2x_frame_list_append(&stream->header_fragments, frame);
        stream->end_header_sent = true;
        parse_header_frames_and_trigger_callback(connection, stream);

    } else {
        h2x_frame_list_append(&stream->header_fragments, frame);
    }

    return H2X_NO_ERROR;
}

h2x_connection_error h2x_connection_handle_inbound_continuation(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {
    if(h2x_frame_get_flags(frame) & H2X_END_HEADERS) {
        h2x_frame_list_append(&stream->header_fragments, frame);
        stream->end_header_sent = true;
        parse_header_frames_and_trigger_callback(connection, stream);

    } else {
        h2x_frame_list_append(&stream->header_fragments, frame);
    }

    return H2X_NO_ERROR;
}

h2x_connection_error h2x_connection_handle_inbound_stream_closed(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {
    return H2X_NO_ERROR;
}

h2x_connection_error h2x_connection_handle_inbound_stream_data(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {

     if(connection->on_stream_body_received) {
         connection->on_stream_body_received(connection, h2x_frame_get_payload(frame), h2x_frame_get_length(frame), stream->stream_identifier,
                                             h2x_frame_get_flags(frame) & H2X_STREAM_CLOSED, stream->user_data);
     }

    return H2X_NO_ERROR;
}

h2x_connection_error h2x_connection_handle_inbound_stream_window_update(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {
    return H2X_NO_ERROR;
}

h2x_connection_error h2x_connection_handle_inbound_stream_priority(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream) {
    return H2X_NO_ERROR;
}

h2x_connection_error h2x_connection_handle_inbound_stream_error(struct h2x_connection* connection, struct h2x_frame* frame, struct h2x_stream* stream, h2x_connection_error error) {
    return H2X_NO_ERROR;
}

void h2x_connection_add_request(struct h2x_connection* connection, struct h2x_request* request)
{
    h2x_thread_add_request(connection->owner, request);
}
