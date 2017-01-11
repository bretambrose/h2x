#include <h2x_connection.h>
#include <h2x_stream.h>
#include <stdlib.h>
#include <memory.h>

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

static uint32_t stream_hash_function(void* arg)
{
    struct h2x_stream* stream = (struct h2x_stream*)arg;
    return stream->stream_identifier;
}

static void get_padding(struct h2x_frame* frame, uint8_t* padding_offset, uint8_t* padding_length)
{
    *padding_offset = 0;
    *padding_length = 0;

    if(h2x_frame_get_flags(frame) & H2X_PADDED)
    {
        *padding_offset = 1;
        *padding_length = h2x_frame_get_payload(frame)[0];
    }
}

void stream_headers_received(struct h2x_stream* stream, struct h2x_frame* frame, void* data)
{
    struct h2x_connection* connection = (struct h2x_connection*)data;

    if(connection->on_stream_headers_received)
    {
        uint8_t padding_offset = 0;
        uint8_t padding_length = 0;

        get_padding(frame, &padding_offset, &padding_length);

        struct h2x_header_list* headers = NULL;//decode and aggregate these.
        connection->on_stream_headers_received(connection, headers,
                                               stream->stream_identifier, connection->user_data);
    }
}

void stream_data_received(struct h2x_stream* stream, struct h2x_frame* frame, bool final_frame, void* data)
{
    struct h2x_connection* connection = (struct h2x_connection*)data;

    if(connection->on_stream_body_received)
    {
        uint8_t padding_offset = 0;
        uint8_t padding_length = 0;

        get_padding(frame, &padding_offset, &padding_length);

        connection->on_stream_body_received(connection, h2x_frame_get_payload(frame) + padding_offset,
                                            h2x_frame_get_length(frame) - padding_length,
                                            stream->stream_identifier, final_frame, connection->user_data);
    }
}

void stream_error(struct h2x_stream* stream, struct h2x_frame* frame, h2x_stream_error error, void* data)
{
    struct h2x_connection* connection = (struct h2x_connection*)data;

    if(connection->on_stream_error)
    {
        connection->on_stream_error(connection, error, stream->stream_identifier, connection->user_data);
    }
}

void h2x_connection_init(struct h2x_connection* connection, struct h2x_thread* owner, int fd, h2x_mode mode)
{
    connection->owner = owner;
    connection->fd = fd;
    connection->mode = mode;
    connection->current_frame_size = 0;
    connection->state = H2X_NOT_ON_FRAME;
    connection->current_outbound_frame = NULL;
    connection->current_outbound_frame_read_position = 0;

    connection->pending_read_chain = NULL;
    connection->pending_write_chain = NULL;
    connection->pending_close_chain = NULL;

    connection->on_stream_headers_received = NULL;
    connection->on_stream_body_received = NULL;
    connection->on_stream_error = NULL;
    connection->user_data = NULL;
    h2x_frame_list_init(&connection->outgoing_frames);

    if(mode == H2X_MODE_SERVER)
    {
        connection->next_outgoing_stream_id = 2;
    }
    else
    {
        connection->next_outgoing_stream_id = 1;
    }

    h2x_hash_table_init(&connection->streams, 50, &stream_hash_function);
}

void h2x_connection_cleanup(struct h2x_connection *connection)
{
    h2x_hash_table_cleanup(&connection->streams);
}

void h2x_connection_on_data_received(struct h2x_connection* connection, uint8_t* data, uint32_t data_length)
{
    uint32_t read = 0;
    uint32_t amount_to_read = 0;

    do
    {
        switch (connection->state)
        {
        case H2X_NOT_ON_FRAME:
            connection->current_frame_read = 0;
            connection->current_frame_size = 0;
            connection->current_frame = (struct h2x_frame*) malloc(sizeof(struct h2x_frame));
            h2x_frame_init(connection->current_frame);

            connection->current_frame->raw_data = (uint8_t*)malloc(MAX_RECV_FRAME_SIZE);
            connection->current_frame->size = MAX_RECV_FRAME_SIZE;
            connection->state = H2X_ON_HEADER;
            break;

        case H2X_ON_HEADER:
            amount_to_read = min(data_length - read, FRAME_HEADER_LENGTH - connection->current_frame_read);
            memcpy(connection->current_frame->raw_data + connection->current_frame_read, data + read, amount_to_read);
            connection->current_frame_read += amount_to_read;
            read += amount_to_read;

            if(amount_to_read >= FRAME_HEADER_LENGTH)
            {
                connection->state = H2X_HEADER_FILLED;
            }
            break;

        case H2X_HEADER_FILLED:
            connection->current_frame_size = h2x_frame_get_length(connection->current_frame) + FRAME_HEADER_LENGTH;
            connection->state = H2X_ON_DATA;
            break;

        case H2X_ON_DATA:
            amount_to_read = min(data_length - read, connection->current_frame_size - FRAME_HEADER_LENGTH - connection->current_frame_read);
            memcpy(connection->current_frame->raw_data + connection->current_frame_read, data + read, amount_to_read);
            read += amount_to_read;
            connection->current_frame_read += amount_to_read;

            if(connection->current_frame_read == connection->current_frame_size)
            {
                h2x_connection_push_frame_to_stream(connection, connection->current_frame);
                connection->state = H2X_NOT_ON_FRAME;
            }
        }
    } while(read < data_length);
}

void set_stream_callbacks(struct h2x_connection* connection, struct h2x_stream* stream)
{
    stream->user_data = connection;
    stream->on_data_received = &stream_data_received;
    stream->on_headers_received = &stream_headers_received;
    stream->on_error = &stream_error;
}

void h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame* frame)
{
    struct h2x_stream* stream = h2x_hash_table_find(&connection->streams, h2x_frame_get_stream_identifier(frame));

    if(!stream)
    {
        stream = (struct h2x_stream*)malloc(sizeof(struct h2x_stream));
        h2x_stream_init(stream);
        stream->stream_identifier = h2x_frame_get_stream_identifier(frame);
        stream->push_dir = H2X_STREAM_INBOUND;
        set_stream_callbacks(connection, stream);
        h2x_hash_table_add(&connection->streams, stream);
    }

    h2x_stream_push_frame(stream, frame, &connection->outgoing_frames);
}

uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection)
{
    uint32_t stream_id = connection->next_outgoing_stream_id;
    connection->next_outgoing_stream_id += 2;

    struct h2x_stream* stream = (struct h2x_stream*)malloc(sizeof(struct h2x_stream));
    h2x_stream_init(stream);
    stream->stream_identifier = stream_id;
    stream->push_dir = H2X_STREAM_OUTBOUND;
    set_stream_callbacks(connection, stream);
    h2x_hash_table_add(&connection->streams, stream);

    return stream_id;
}

struct h2x_frame* h2x_connection_pop_frame(struct h2x_connection* connection)
{
    return h2x_frame_list_pop(&connection->outgoing_frames);
}

void h2x_connection_set_stream_headers_receieved_callback(struct h2x_connection* connection,
                                                          void(*callback)(struct h2x_connection*, struct h2x_header_list* headers, uint32_t, void*))
{
    connection->on_stream_headers_received = callback;
}

void h2x_connection_set_stream_body_receieved_callback(struct h2x_connection* connection,
                                                       void(*callback)(struct h2x_connection*, uint8_t* data, uint32_t length, uint32_t, bool finalFrame, void*))
{
    connection->on_stream_body_received = callback;
}

void h2x_connection_set_stream_error_callback(struct h2x_connection* connection,
                                              void(*callback)(struct h2x_connection*, h2x_stream_error, uint32_t, void*))
{
    connection->on_stream_error = callback;
}

void h2x_connection_set_user_data(struct h2x_connection* connection, void* user_data)
{
    connection->user_data = user_data;
}

/*
static bool has_outbound_data(struct h2x_connection* connection)
{
    // TODO
    return connection->current_outbound_frame != NULL;// || ??
}
*/

void h2x_connection_on_new_outbound_data(struct h2x_connection* connection)
{
    /*
     TODO
    if(!connection->subscribed_to_write_events)
    {
        event.events = EPOLLIN | EPOLLET | EPOLLPRI | EPOLLERR | EPOLLOUT | EPOLLRDHUP | EPOLLHUP;
        ??;

        subscribed_to_write_events = true;
    }
     */
}

bool h2x_connection_write_outbound_data(struct h2x_connection* connection)
{
    /*
    TODO

    ??;

    // if there's nothing left to write then we can remove the write event until
    // something new needs to be written
    if(??)
    {
        ??;
    }
     */

    return false;
}
