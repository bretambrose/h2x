#ifndef H2X_STREAM_H
#define H2X_STREAM_H

#include <h2x_enum_types.h>
#include <h2x_frame.h>
#include <stdbool.h>
#include <stdint.h>

struct h2x_frame;
struct h2x_frame_list;

struct h2x_stream
{
    uint32_t stream_identifier;
    h2x_stream_push_dir push_dir;
    h2x_stream_state state;
    void* user_data;
    struct h2x_frame_list header_fragments;
    bool end_header_sent;
};

void h2x_stream_init(struct h2x_stream* stream);

void h2x_stream_clean(struct h2x_stream* stream);

void h2x_stream_append_header_fragment(struct h2x_stream* stream, struct h2x_frame* frame);

void h2x_stream_set_state(struct h2x_stream* stream, h2x_stream_state state);

#endif // H2X_STREAM_H
