
#ifndef H2X_H2X_FRAME_H_H
#define H2X_H2X_FRAME_H_H

#include <stdint.h>

struct h2x_frame
{
    uint8_t* raw_data;
    uint32_t size;
};

void h2x_frame_init(struct h2x_frame* frame);

uint32_t h2x_frame_get_length(struct h2x_frame* frame);

uint8_t* h2x_frame_get_payload(struct h2x_frame* frame);

uint32_t h2x_frame_get_stream_identifier(struct h2x_frame* frame);

uint8_t h2x_frame_get_flags(struct h2x_frame* frame);

uint8_t h2x_frame_get_type(struct h2x_frame* frame);

uint8_t h2x_frame_get_r(struct h2x_frame* frame);

#endif //H2X_H2X_FRAME_H_H
