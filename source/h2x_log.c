#include <h2x_log.h>

#include <h2x_options.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

h2x_log_level g_log_level = H2X_LOG_LEVEL_ERROR;

static h2x_log_dest s_log_dest = H2X_LOG_DEST_STDERR;
static FILE* s_log_fp = NULL;
static bool s_log_synchronize = false;
static pthread_mutex_t s_log_lock;

void h2x_logging_init(struct h2x_options* options)
{
    g_log_level = options->log_level;
    s_log_dest = options->log_dest;
    s_log_synchronize = options->sync_logging;
    if(s_log_dest == H2X_LOG_DEST_FILE)
    {
        char* log_filename = options->log_filename;
        if(log_filename == NULL)
        {
            if(options->mode == H2X_MODE_SERVER)
            {
                log_filename = "h2x_server.log";
            }
            else if(options->mode == H2X_MODE_CLIENT)
            {
                log_filename = "h2x_client.log";
            }
            else
            {
                log_filename = "h2x_unknown.log";
            }
        }

        s_log_fp = fopen(log_filename, "w");
    }

    if(s_log_synchronize)
    {
        pthread_mutex_init(&s_log_lock, NULL);
    }
}

void h2x_logging_cleanup()
{
    if(s_log_fp != NULL)
    {
        fclose(s_log_fp);
    }

    if(s_log_synchronize)
    {
        pthread_mutex_destroy(&s_log_lock);
    }
}

void h2x_log(h2x_log_level log_level, const char* format_str, ...)
{
    if(s_log_dest == H2X_LOG_DEST_NONE)
    {
        return;
    }

    va_list args;
    va_start(args, format_str);

    va_list tmp_args; //unfortunately you cannot consume a va_list twice
    va_copy(tmp_args, args); //so we have to copy it
    int required_length = vsnprintf(NULL, 0, format_str, tmp_args) + 1;
    va_end(tmp_args);

    char buffer[8192];
    if(required_length > (int)sizeof(buffer))
    {
        H2X_LOG(H2X_LOG_LEVEL_ERROR, "Rejecting excessively large log call (likely corrupt) of length %d\n", required_length);
        return;
    }

    vsnprintf(buffer, required_length, format_str, args);

    if(s_log_synchronize)
    {
        pthread_mutex_lock(&s_log_lock);
    }

    switch(s_log_dest)
    {
        case H2X_LOG_DEST_FILE:
            fprintf(s_log_fp, "[%s] %s\n", h2x_log_level_to_string(log_level), buffer);
            break;

        case H2X_LOG_DEST_STDERR:
            fprintf(stderr, "[%s] %s\n", h2x_log_level_to_string(log_level), buffer);
            break;

        default:
            break;
    }

    if(s_log_synchronize)
    {
        pthread_mutex_unlock(&s_log_lock);
    }
}
