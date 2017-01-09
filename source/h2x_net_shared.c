#include <h2x_net_shared.h>

#include <unistd.h>
#include <fcntl.h>

int h2x_make_socket_nonblocking(int socket_fd)
{
  int flags, ret_val;

  flags = fcntl(socket_fd, F_GETFL, 0);
  if(flags == -1)
  {
    return -1;
  }

  flags |= O_NONBLOCK;
  ret_val = fcntl (socket_fd, F_SETFL, flags);
  if(ret_val == -1)
  {
    return -1;
  }

  return 0;
}

bool h2x_is_little_endian_system()
{
    int i = 1;
    return (int)*((unsigned char *)&i)==1;
}

void h2x_set_integer_as_big_endian(uint8_t* to_set, uint32_t int_value)
{
    uint16_t data_index = 0;
    uint16_t int_index = 0;

    if(h2x_is_little_endian_system())
    {
        int_index = sizeof(uint32_t) - 1;
    }

    uint8_t* int_as_buffer = (uint8_t*)&int_value;

    for(;data_index < sizeof(uint32_t); ++data_index, --int_index)
    {
        to_set[data_index] = int_as_buffer[int_index];
    }
}
