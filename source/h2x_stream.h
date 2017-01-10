#ifndef H2X_H2X_STREAM_H
#define H2X_H2X_STREAM_H

#include <stdbool.h>
#include <stdint.h>

enum H2X_STREAM_STATE
{
    IDLE,
    RESERVED_LOCAL,
    RESERVED_REMOTE,
    OPEN,
    HALF_CLOSED_LOCAL,
    HALF_CLOSED_REMOTE,
    CLOSED
};

enum H2X_STREAM_PUSH_DIR
{
    STREAM_NOT_INIT,
    STREAM_INBOUND,
    STREAM_OUTBOUND,
};

enum H2X_STREAM_ERROR
{
    STREAM_NO_ERROR = 0x00,
    STREAM_PROTOCOL_ERROR = 0x01,
    STREAM_CLOSED = 0x05
};

struct h2x_frame;
struct h2x_frame_list;

struct h2x_stream
{
    uint32_t stream_identifier;
    enum H2X_STREAM_PUSH_DIR push_dir;
    enum H2X_STREAM_STATE state;
    void* user_data;
    void(*on_headers_received)(struct h2x_stream*, struct h2x_frame*, void*);
    void(*on_data_received)(struct h2x_stream*, struct h2x_frame*, bool final_frame, void*);
    void(*on_error)(struct h2x_stream*, struct h2x_frame*, enum H2X_STREAM_ERROR, void*);
};

void h2x_stream_init(struct h2x_stream* stream);

void h2x_stream_push_frame(struct h2x_stream* stream, struct h2x_frame* frame, struct h2x_frame_list* frame_list);

void h2x_stream_set_state(struct h2x_stream* stream, enum H2X_STREAM_STATE state);

#endif //H2X_H2X_STREAM_H
