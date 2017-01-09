#include <h2x_connection.h>

#include <stdlib.h>

struct h2x_connection* h2x_connection_init(int fd)
{
    struct h2x_connection* connection = malloc(sizeof(struct h2x_connection));
    connection->fd = fd;

    return connection;
}

void h2x_connection_cleanup(struct h2x_connection *connection)
{
    free(connection);
}

void h2x_connection_on_data_received(struct h2x_connection *connection, uint8_t *data, uint32_t data_length)
{
    ;
}

