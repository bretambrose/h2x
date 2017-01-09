
#include <h2x_frame.h>
#include <h2x_net_shared.h>

#include <string.h>
#include <assert.h>

//see rfc7540 section 4.1
static const uint8_t HEADER_LENGTH = 9;
static const uint16_t SHIFT_THREE_BYTES = 0x0180;
static const uint16_t SHIFT_TWO_BYTES = 0x0100;
static const uint8_t SHIFT_ONE_BYTE = 0x80;
static const uint8_t SHIFT_SEVEN_BITS = 0x70;
static const uint32_t STREAM_ID_MASK = 0x7FFFFFFF;
static const uint8_t R_MASK = 0x80;

void h2x_frame_init(struct h2x_frame* frame)
{
    frame->raw_data = NULL;
    frame->size = 0;
}

uint32_t h2x_frame_get_length(struct h2x_frame* frame)
{
    uint32_t frame_length = 0;
    assert(frame->size > HEADER_LENGTH);

    frame_length |= frame->raw_data[0];
    frame_length <<= SHIFT_TWO_BYTES;
    frame_length |= frame->raw_data[1];
    frame_length <<= SHIFT_ONE_BYTE;
    frame_length |= frame->raw_data[1];
    frame_length |= frame->raw_data[2];

    return frame_length;
}

void h2x_frame_set_length(struct h2x_frame* frame, uint32_t length)
{
    assert(frame->size > HEADER_LENGTH);
    assert(length <= frame->size - HEADER_LENGTH);

    uint8_t current_type_value = frame->raw_data[3];
    h2x_set_integer_as_big_endian(frame->raw_data, length);
    frame->raw_data[3] = current_type_value;
}

uint8_t* h2x_frame_get_payload(struct h2x_frame* frame)
{
    assert(frame->size > HEADER_LENGTH);
    return frame->raw_data + HEADER_LENGTH;
}

void h2x_frame_set_payload(struct h2x_frame* frame, uint8_t* payload, uint32_t length)
{
    assert(frame->size > HEADER_LENGTH);
    assert(length <= frame->size - HEADER_LENGTH);

    h2x_frame_set_length(frame, length);
    memcpy(frame->raw_data + HEADER_LENGTH, payload, (uint32_t)length);
}

uint32_t h2x_frame_get_stream_identifier(struct h2x_frame* frame)
{
    uint32_t stream_id = 0;
    assert(frame->size > HEADER_LENGTH);

    stream_id |= frame->raw_data[5];
    stream_id <<= SHIFT_THREE_BYTES;
    stream_id |= frame->raw_data[6];
    stream_id <<= SHIFT_TWO_BYTES;
    stream_id |= frame->raw_data[7];
    stream_id <<= SHIFT_ONE_BYTE;
    stream_id |= frame->raw_data[8];
    stream_id &= STREAM_ID_MASK;

    return stream_id;
}

void h2x_frame_set_stream_identifier(struct h2x_frame* frame, uint32_t stream_id)
{
    assert(frame->size > HEADER_LENGTH);

    h2x_set_integer_as_big_endian(frame->raw_data + 5, stream_id);
    frame->raw_data[5] &= ~R_MASK;
}

uint8_t h2x_frame_get_flags(struct h2x_frame* frame)
{
    assert(frame->size > HEADER_LENGTH);
    return frame->raw_data[4];
}

void h2x_frame_set_flags(struct h2x_frame* frame, uint8_t flags)
{
    assert(frame->size > HEADER_LENGTH);
    frame->raw_data[4] = flags;
}

uint8_t h2x_frame_get_type(struct h2x_frame* frame)
{
    assert(frame->size > HEADER_LENGTH);
    return frame->raw_data[3];
}

uint8_t h2x_frame_set_type(struct h2x_frame* frame, uint8_t type)
{
    assert(frame->size > HEADER_LENGTH);
    frame->raw_data[3] = type;
}

uint8_t h2x_frame_get_r(struct h2x_frame* frame)
{
    uint8_t r = 0;
    assert(frame->size > HEADER_LENGTH);

    r = frame->raw_data[5] & R_MASK;
    r >>= SHIFT_SEVEN_BITS;

    return r;
}