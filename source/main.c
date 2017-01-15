#include <s2n.h>

#include <stdio.h>
#include <stdlib.h>

#include <h2x_buffer.h>
#include <h2x_client.h>
#include <h2x_log.h>
#include <h2x_options.h>
#include <h2x_server.h>

int main(int argc, char **argv)
{
    struct h2x_options options;
    if(h2x_options_init(&options, argc, argv))
    {
        h2x_print_usage(argv[0]);
        h2x_options_cleanup(&options);
        return 1;
    }

    h2x_logging_init(&options);

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
        H2X_LOG(H2X_LOG_LEVEL_INFO, "No mode selected.  Exiting...");
    }

    s2n_cleanup();

    h2x_logging_cleanup();

    h2x_options_cleanup(&options);

    return 0;
}
