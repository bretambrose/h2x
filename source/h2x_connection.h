
#ifndef H2X_CONNECTION_H
#define H2X_CONNECTION_H

#include <stdint.h>

#include <h2x_options.h>

struct h2x_connection {
    int fd;
    h2x_mode mode;
};

struct h2x_connection* h2x_connection_init(int fd, h2x_mode mode);
void h2x_connection_cleanup(struct h2x_connection *connection);
void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t *data, uint32_t data_length);

#endif // H2X_CONNECTION_H
