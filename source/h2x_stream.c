
#include <h2x_stream.h>
#include <h2x_frame.h>
#include <stddef.h>

static bool set_can_send_if_outbound(struct h2x_stream* stream)
{
    if(stream->push_dir == H2X_STREAM_OUTBOUND)
    {
        return true;
    }

    return false;
}

void h2x_stream_init(struct h2x_stream* stream)
{
    stream->stream_identifier = 0;
    stream->state = H2X_IDLE;
    stream->user_data = NULL;
    stream->on_headers_received = NULL;
    stream->push_dir = H2X_STREAM_NOT_INIT;
}

void h2x_stream_push_frame(struct h2x_stream* stream, struct h2x_frame* frame, struct h2x_frame_list* frame_list)
{
    h2x_stream_state cur_state = stream->state;
    h2x_frame_type frame_type = h2x_frame_get_type(frame);
    bool should_send = false;

    switch(cur_state)
    {
    case H2X_IDLE:
        if (frame_type == H2X_HEADERS)
        {
            stream->state = H2X_OPEN;

            if (stream->push_dir == H2X_STREAM_INBOUND && stream->on_headers_received)
            {
                stream->on_headers_received(stream, frame, stream->user_data);
            }

            should_send = set_can_send_if_outbound(stream);
            break;
        }
        else if (frame_type == H2X_PRIORITY)
        {
            should_send = set_can_send_if_outbound(stream);
            //do priority stuff here.
            break;
        }

        stream->state = H2X_CLOSED;
        if (stream->on_error)
        {
            stream->on_error(stream, frame, H2X_STREAM_PROTOCOL_ERROR, stream->user_data);
        }
        break;

    case H2X_RESERVED_LOCAL:
        if (frame_type == H2X_RST_STREAM)
        {
            should_send = set_can_send_if_outbound(stream);
            stream->state = H2X_CLOSED;
            break;
        }

        if (stream->push_dir == H2X_STREAM_OUTBOUND)
        {
            if (frame_type == H2X_HEADERS)
            {
                should_send = set_can_send_if_outbound(stream);
                stream->state = H2X_HALF_CLOSED_REMOTE;
                break;
            }

            if (frame_type == H2X_PRIORITY)
            {
                should_send = set_can_send_if_outbound(stream);
                //priority stuff here:
                break;
            }
        }
        else
        {
            stream->state = H2X_CLOSED;
            if (stream->on_error)
            {
                stream->on_error(stream, frame, H2X_STREAM_PROTOCOL_ERROR, stream->user_data);
            }
            break;
        }

        break;

    case H2X_RESERVED_REMOTE:
        if (frame_type == H2X_RST_STREAM)
        {
            should_send = set_can_send_if_outbound(stream);
            stream->state = H2X_CLOSED;
            break;
        }

        if (frame_type == H2X_PRIORITY || frame_type == H2X_WINDOW_UPDATE)
        {
            should_send = set_can_send_if_outbound(stream);
            break;
        }

        if (stream->push_dir == H2X_STREAM_INBOUND)
        {
            if (frame_type == H2X_HEADERS)
            {
                stream->state = H2X_HALF_CLOSED_LOCAL;

                if (stream->on_headers_received)
                {
                    stream->on_headers_received(stream, frame, stream->user_data);
                }
                break;
            }
        }
        break;

    case H2X_OPEN:
        should_send = set_can_send_if_outbound(stream);

        if (frame_type == H2X_RST_STREAM)
        {
            stream->state = H2X_CLOSED;
            break;
        }

        if(frame_type == H2X_DATA)
        {
            bool finalFrame = false;
            if (h2x_frame_get_flags(frame) | H2X_END_STREAM)    // TODO: fix this
            {
                finalFrame = true;
                stream->state = stream->push_dir == H2X_STREAM_INBOUND ? H2X_HALF_CLOSED_REMOTE : H2X_HALF_CLOSED_LOCAL;
            }

            if(stream->push_dir == H2X_STREAM_INBOUND)
            {
                if (stream->on_data_received)
                {
                    stream->on_data_received(stream, frame, finalFrame, stream->user_data);
                }
            }
        }

        break;
    case H2X_HALF_CLOSED_LOCAL:
        if(frame_type == H2X_RST_STREAM)
        {
            should_send = set_can_send_if_outbound(stream);
            stream->state = H2X_CLOSED;
            break;
        }

        if(stream->push_dir == H2X_STREAM_INBOUND)
        {
            if(h2x_frame_get_flags(frame) | H2X_END_STREAM)     // TODO: this
            {
                stream->state = H2X_CLOSED;
                break;
            }
            break;
        }
        else
        {
            if(frame_type == H2X_WINDOW_UPDATE || frame_type == H2X_PRIORITY)
            {
                should_send = set_can_send_if_outbound(stream);
                //do stuff.

            }
            break;
        }

    case H2X_HALF_CLOSED_REMOTE:
        if(stream->push_dir == H2X_STREAM_INBOUND)
        {
            if(frame_type == H2X_WINDOW_UPDATE || frame_type == H2X_PRIORITY || frame_type == H2X_RST_STREAM)
            {
                //do stuff.

            }
            else
            {
                stream->state = H2X_CLOSED;
                if (stream->on_error)
                {
                    stream->on_error(stream, frame, H2X_STREAM_CLOSED, stream->user_data);
                }
            }
            break;
        }

        if(frame_type == H2X_RST_STREAM ||
                (frame_type == H2X_DATA && h2x_frame_get_flags(frame) | H2X_END_STREAM))    // TODO: vut iz dis?
        {
            stream->state = H2X_CLOSED;
            should_send = set_can_send_if_outbound(stream);
            break;
        }

        should_send = true;
        break;
    case H2X_CLOSED:
        //maybe handle priority here?
        break;
    default:
        break;
    }

    if(should_send)
    {
        h2x_frame_list_append(frame_list, frame);
    }
}

void h2x_stream_set_state(struct h2x_stream* stream, h2x_stream_state state)
{
    stream->state = state;
}
