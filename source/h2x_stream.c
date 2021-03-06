
#include <h2x_stream.h>
#include <h2x_frame.h>
#include <h2x_log.h>

#include <stddef.h>

void h2x_stream_init(struct h2x_stream* stream)
{
    stream->stream_identifier = 0;
    stream->state = H2X_IDLE;
    h2x_frame_list_init(&stream->header_fragments);
    stream->end_header_sent = false;
    stream->user_data = NULL;
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
    h2x_stream_state current_state = stream->state;
    stream->state = state;
    H2X_LOG(H2X_LOG_LEVEL_DEBUG, "stream %u state transition %s -> %s", stream->stream_identifier, h2x_stream_state_to_string(current_state), h2x_stream_state_to_string(state));
}
