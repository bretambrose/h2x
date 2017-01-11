
#ifndef H2X_FRAME_H_H
#define H2X_FRAME_H_H

#include <h2x_enum_types.h>

#include <stdint.h>

static const uint8_t FRAME_HEADER_LENGTH = 9;

struct h2x_frame
{
    uint8_t* raw_data;
    uint32_t size;
};


void h2x_frame_init(struct h2x_frame* frame);

void h2x_frame_cleanup(struct h2x_frame* frame);

uint32_t h2x_frame_get_length(struct h2x_frame* frame);

void h2x_frame_set_length(struct h2x_frame* frame, uint32_t length);

uint8_t* h2x_frame_get_payload(struct h2x_frame* frame);

void h2x_frame_set_payload(struct h2x_frame* frame, uint8_t* payload, uint32_t length);

uint32_t h2x_frame_get_stream_identifier(struct h2x_frame* frame);

void h2x_frame_set_stream_identifier(struct h2x_frame* frame, uint32_t stream_id);

uint8_t h2x_frame_get_flags(struct h2x_frame* frame);

void h2x_frame_set_flags(struct h2x_frame* frame, uint8_t flags);

h2x_frame_type h2x_frame_get_type(struct h2x_frame* frame);

void h2x_frame_set_type(struct h2x_frame* frame, h2x_frame_type type);

uint8_t h2x_frame_get_r(struct h2x_frame* frame);

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

void h2x_frame_list_clean(struct h2x_frame_list* list);

struct h2x_frame* h2x_frame_list_top(struct h2x_frame_list* list);

struct h2x_frame* h2x_frame_list_pop(struct h2x_frame_list* list);

void h2x_frame_list_append(struct h2x_frame_list* list, struct h2x_frame* frame);

#endif // H2X_FRAME_H_H
