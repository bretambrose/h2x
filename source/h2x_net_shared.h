
#ifndef H2X_NET_SHARED_H
#define H2X_NET_SHARED_H

#include <stdbool.h>
#include <stdint.h>

void *h2x_processing_thread_function(void * arg);

int h2x_make_socket_nonblocking(int socket_fd);

bool h2x_is_little_endian_system();

void h2x_set_integer_as_big_endian(uint8_t* to_set, uint32_t int_value);

#endif // H2X_NET_SHARED_H
