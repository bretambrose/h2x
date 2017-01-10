
#ifndef H2X_H2X_FRAME_H_H
#define H2X_H2X_FRAME_H_H

#include <stdint.h>

static const uint8_t FRAME_HEADER_LENGTH = 9;

struct h2x_frame
{
    uint8_t* raw_data;
    uint32_t size;
};

void h2x_frame_init(struct h2x_frame* frame);

uint32_t h2x_frame_get_length(struct h2x_frame* frame);

void h2x_frame_set_length(struct h2x_frame* frame, uint32_t length);

uint8_t* h2x_frame_get_payload(struct h2x_frame* frame);

void h2x_frame_set_payload(struct h2x_frame* frame, uint8_t* payload, uint32_t length);

uint32_t h2x_frame_get_stream_identifier(struct h2x_frame* frame);

void h2x_frame_set_stream_identifier(struct h2x_frame* frame, uint32_t stream_id);

uint8_t h2x_frame_get_flags(struct h2x_frame* frame);

void h2x_frame_set_flags(struct h2x_frame* frame, uint8_t flags);

enum H2X_FRAME_TYPE h2x_frame_get_type(struct h2x_frame* frame);

uint8_t h2x_frame_set_type(struct h2x_frame* frame, enum H2X_FRAME_TYPE type);

uint8_t h2x_frame_get_r(struct h2x_frame* frame);

enum H2X_FRAME_TYPE
{
    DATA = 0x00,
    HEADERS = 0x01,
    PRIORITY = 0x02,
    RST_STREAM = 0x03,
    SETTINGS = 0x04,
    PUSH_PROMISE = 0x05,
    PING = 0x06,
    GOAWAY = 0x07,
    WINDOW_UPDATE = 0x08,
    CONTINUATION = 0x09
};

enum H2X_FRAME_FLAGS
{
    END_STREAM = 0x01,
    PADDED = 0x08
};

struct h2x_frame_list_node
{
    struct h2x_frame* frame;
    struct h2x_frame_list_node* next;
};

struct h2x_frame_list
{
    uint16_t frame_count;
    struct h2x_frame_list_node* head;
    struct h2x_frame_list_node* tail;
};

void h2x_frame_list_init(struct h2x_frame_list* list);

struct h2x_frame* h2x_frame_list_top(struct h2x_frame_list* list);

struct h2x_frame* h2x_frame_list_pop(struct h2x_frame_list* list);

void h2x_frame_list_append(struct h2x_frame_list* list, struct h2x_frame*);

#endif //H2X_H2X_FRAME_H_H
