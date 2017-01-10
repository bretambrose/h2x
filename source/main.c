#include <s2n.h>

#include <stdio.h>

#include <h2x_options.h>
#include <h2x_buffer.h>
#include <h2x_client.h>
#include <h2x_server.h>

int main(int argc, char **argv)
{
    struct h2x_options options;
    if(h2x_parse_options(argc, argv, &options))
    {
        h2x_print_usage(argv[0]);
        return 1;
    }

    s2n_init();

    if(options.mode == H2X_MODE_CLIENT)
    {
        h2x_do_client(&options);
    }
    else if(options.mode == H2X_MODE_SERVER)
    {
        h2x_do_server(&options);
    }
    else
    {
        fprintf(stderr, "No mode selected\n");
    }

    s2n_cleanup();

    return 0;
}
