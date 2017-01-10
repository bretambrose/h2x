#include <h2x_connection.h>

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

void h2x_connection_init(struct h2x_connection* connection, int fd, h2x_mode mode)
{
    connection->fd = fd;
    connection->mode = mode;
    connection->current_frame_size = 0;
    connection->state = NOT_ON_FRAME;

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
        case NOT_ON_FRAME:
            connection->current_frame_read = 0;
            connection->current_frame_size = 0;
            connection->current_frame = (struct h2x_frame*) malloc(sizeof(struct h2x_frame));
            h2x_frame_init(connection->current_frame);

            connection->current_frame->raw_data = (uint8_t*)malloc(MAX_RECV_FRAME_SIZE);
            connection->current_frame->size = MAX_RECV_FRAME_SIZE;
            connection->state = ON_HEADER;
            break;

        case ON_HEADER:
            amount_to_read = min(data_length - read, FRAME_HEADER_LENGTH - connection->current_frame_read);
            memcpy(connection->current_frame->raw_data + connection->current_frame_read, data + read, amount_to_read);
            connection->current_frame_read += amount_to_read;
            read += amount_to_read;

            if(amount_to_read >= FRAME_HEADER_LENGTH)
            {
                connection->state = HEADER_FILLED;
            }
            break;

        case HEADER_FILLED:
            connection->current_frame_size = h2x_frame_get_length(connection->current_frame) + FRAME_HEADER_LENGTH;
            connection->state = ON_DATA;
            break;

        case ON_DATA:
            amount_to_read = min(data_length - read, connection->current_frame_size - FRAME_HEADER_LENGTH - connection->current_frame_read);
            memcpy(connection->current_frame->raw_data + connection->current_frame_read, data + read, amount_to_read);
            read += amount_to_read;
            connection->current_frame_read += amount_to_read;

            if(connection->current_frame_read == connection->current_frame_size)
            {
                h2x_connection_push_frame_to_stream(connection, connection->current_frame);
                connection->state = NOT_ON_FRAME;
            }
        }
    } while(read < data_length);
}

uint32_t h2x_connection_push_frame_to_stream(struct h2x_connection *connection, struct h2x_frame* frame)
{
    struct h2x_stream* stream = h2x_hash_table_find(&connection->streams, h2x_frame_get_stream_identifier(frame));

    if(!stream)
    {
        stream = (struct h2x_stream*)malloc(sizeof(struct h2x_stream));
        h2x_stream_init(stream);
        stream->stream_identifier = h2x_frame_get_stream_identifier(frame);
        stream->push_dir = STREAM_INBOUND;
        h2x_hash_table_add(&connection->streams, stream);
    }

    h2x_stream_push_frame(stream, frame);
}

uint32_t h2x_connection_create_outbound_stream(struct h2x_connection *connection)
{
    uint32_t stream_id = connection->next_outgoing_stream_id;
    connection->next_outgoing_stream_id += 2;

    struct h2x_stream* stream = (struct h2x_stream*)malloc(sizeof(struct h2x_stream));
    h2x_stream_init(stream);
    stream->stream_identifier = stream_id;
    stream->push_dir = STREAM_OUTBOUND;
    h2x_hash_table_add(&connection->streams, stream);

    return stream_id;
}

