#include <h2x_buffer.h>

#include <stdlib.h>
#include <string.h>

void h2x_buffer_init(uint32_t size, struct h2x_buffer* buffer)
{
    buffer->data = malloc(size);
    buffer->size = size;
    buffer->write_position = 0;
}

void h2x_buffer_write(char* data, uint32_t data_length, struct h2x_buffer* buffer)
{
    if(data_length + buffer->write_position >= buffer->size)
    {
        uint32_t new_size = (uint32_t)((data_length + buffer->write_position) * 1.4);
        buffer->data = realloc(buffer->data, new_size);
        buffer->size = new_size;
    }

    memcpy(buffer->data, data, data_length);
    buffer->write_position += data_length;
}

void h2x_buffer_free(struct h2x_buffer *buffer)
{
    if(buffer->data)
    {
        free(buffer->data);
        buffer->data = NULL;
        buffer->size = 0;
        buffer->write_position = 0;
    }
}
