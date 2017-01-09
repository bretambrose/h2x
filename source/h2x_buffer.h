
#ifndef H2X_BUFFER_H
#define H2X_BUFFER_H

#include <stdint.h>

struct h2x_buffer {
    char* data;
    uint32_t size;
    uint32_t write_position;
};

void h2x_buffer_init(uint32_t size, struct h2x_buffer* buffer);
void h2x_buffer_write(char* data, uint32_t data_length, struct h2x_buffer* buffer);
void h2x_buffer_free(struct h2x_buffer *buffer);

#endif // H2X_BUFFER_H
