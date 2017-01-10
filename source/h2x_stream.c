
#include <h2x_stream.h>
#include <stddef.h>

static bool set_can_send_if_outbound(struct h2x_stream* stream)
{
    if(stream->push_dir == STREAM_OUTBOUND)
    {
        return true;
    }

    return false;
}

void h2x_stream_init(struct h2x_stream* stream)
{
    h2x_frame_list_init(&stream->outgoing_frames);
    stream->stream_identifier = 0;
    stream->state = IDLE;
    stream->on_headers_received = NULL;
    stream->push_dir = STREAM_NOT_INIT;
}

void h2x_stream_push_frame(struct h2x_stream* stream, struct h2x_frame* frame)
{
    enum H2X_STREAM_STATE cur_state = stream->state;
    enum H2X_FRAME_TYPE frame_type = h2x_frame_get_type(frame);
    bool should_send = false;

    switch(cur_state)
    {
    case IDLE:
        if (frame_type == HEADERS)
        {
            stream->state = OPEN;

            if (stream->push_dir == STREAM_INBOUND && stream->on_headers_received)
            {
                stream->on_headers_received(stream, frame);
            }

            should_send = set_can_send_if_outbound(stream);
            break;
        }
        else if (frame_type == PRIORITY)
        {
            should_send = set_can_send_if_outbound(stream);
            //do priority stuff here.
            break;
        }

        stream->state = CLOSED;
        if (stream->on_error)
        {
            stream->on_error(stream, frame, STREAM_PROTOCOL_ERROR);
        }
        break;

    case RESERVED_LOCAL:
        if (frame_type == RST_STREAM)
        {
            should_send = set_can_send_if_outbound(stream);
            stream->state = CLOSED;
            break;
        }

        if (stream->push_dir == STREAM_OUTBOUND)
        {
            if (frame_type == HEADERS)
            {
                should_send = set_can_send_if_outbound(stream);
                stream->state = HALF_CLOSED_REMOTE;
                break;
            }

            if (frame_type == PRIORITY)
            {
                should_send = set_can_send_if_outbound(stream);
                //priority stuff here:
                break;
            }
        }
        else
        {
            stream->state = CLOSED;
            if (stream->on_error)
            {
                stream->on_error(stream, frame, STREAM_PROTOCOL_ERROR);
            }
            break;
        }

        break;

    case RESERVED_REMOTE:
        if (frame_type == RST_STREAM)
        {
            should_send = set_can_send_if_outbound(stream);
            stream->state = CLOSED;
            break;
        }

        if (frame_type == PRIORITY || frame_type == WINDOW_UPDATE)
        {
            should_send = set_can_send_if_outbound(stream);
            break;
        }

        if (stream->push_dir == STREAM_INBOUND)
        {
            if (frame_type == HEADERS)
            {
                stream->state = HALF_CLOSED_LOCAL;

                if (stream->on_headers_received)
                {
                    stream->on_headers_received(stream, frame);
                }
                break;
            }
        }
        break;

    case OPEN:
        should_send = set_can_send_if_outbound(stream);

        if (frame_type == RST_STREAM)
        {
            stream->state = CLOSED;
            break;
        }

        if(frame_type == DATA)
        {
            bool finalFrame = false;
            if (h2x_frame_get_flags(frame) | END_STREAM)
            {
                finalFrame = true;
                stream->state = stream->push_dir == STREAM_INBOUND ? HALF_CLOSED_REMOTE : HALF_CLOSED_LOCAL;
            }

            if(stream->push_dir == STREAM_INBOUND)
            {
                if (stream->on_data_received)
                {
                    stream->on_data_received(stream, frame, finalFrame);
                }
            }
        }

        break;
    case HALF_CLOSED_LOCAL:
        if(frame_type == RST_STREAM)
        {
            should_send = set_can_send_if_outbound(stream);
            stream->state = CLOSED;
            break;
        }

        if(stream->push_dir == STREAM_INBOUND)
        {
            if(h2x_frame_get_flags(frame) | END_STREAM)
            {
                stream->state = CLOSED;
                break;
            }
            break;
        }
        else
        {
            if(frame_type == WINDOW_UPDATE || frame_type == PRIORITY)
            {
                should_send = set_can_send_if_outbound(stream);
                //do stuff.

            }
            break;
        }

    case HALF_CLOSED_REMOTE:
        if(stream->push_dir == STREAM_INBOUND)
        {
            if(frame_type == WINDOW_UPDATE || frame_type == PRIORITY || frame_type == RST_STREAM)
            {
                //do stuff.

            }
            else
            {
                stream->state = CLOSED;
                if (stream->on_error)
                {
                    stream->on_error(stream, frame, STREAM_CLOSED);
                }
            }
            break;
        }

        if(frame_type == RST_STREAM ||
                (frame_type == DATA && h2x_frame_get_flags(frame) | END_STREAM))
        {
            stream->state = CLOSED;
            should_send = set_can_send_if_outbound(stream);
            break;
        }

        should_send = true;
        break;
    case CLOSED:
        //maybe handle priority here?
        break;
    default:
        break;
    }

    if(should_send)
    {
        h2x_frame_list_append(&stream->outgoing_frames, frame);
    }
}

void h2x_stream_set_state(struct h2x_stream* stream, enum H2X_STREAM_STATE state)
{
    stream->state = state;
}

struct h2x_frame* h2x_stream_pop_frame(struct h2x_stream* stream)
{
    return h2x_frame_list_pop(&stream->outgoing_frames);
}