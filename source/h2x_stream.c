
#include <h2x_stream.h>
#include <h2x_frame.h>
#include <stddef.h>

void h2x_stream_init(struct h2x_stream* stream)
{
    stream->stream_identifier = 0;
    stream->state = H2X_IDLE;
    h2x_frame_list_init(&stream->header_fragments);
    stream->push_dir = H2X_STREAM_NOT_INIT;
    stream->end_header_sent = false;
}

void h2x_stream_clean(struct h2x_stream* stream)
{
    h2x_frame_list_clean(&stream->header_fragments);
}

void h2x_stream_append_header_fragment(struct h2x_stream* stream, struct h2x_frame* frame)
{
    stream->end_header_sent = h2x_frame_get_flags(frame) & H2X_END_HEADERS;
    h2x_frame_list_append(&stream->header_fragments, frame);
}

void h2x_stream_set_state(struct h2x_stream* stream, h2x_stream_state state)
{
    stream->state = state;
}
