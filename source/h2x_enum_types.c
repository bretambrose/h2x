#include <h2x_enum_types.h>

#include <strings.h>

char* h2x_log_level_to_string(h2x_log_level log_level)
{
    switch(log_level)
    {
        case H2X_LOG_LEVEL_OFF:
            return "OFF";

        case H2X_LOG_LEVEL_FATAL:
            return "FATAL";

        case H2X_LOG_LEVEL_ERROR:
            return "ERROR";

        case H2X_LOG_LEVEL_WARN:
            return "WARN";

        case H2X_LOG_LEVEL_INFO:
            return "INFO";

        case H2X_LOG_LEVEL_DEBUG:
            return "DEBUG";

        case H2X_LOG_LEVEL_TRACE:
            return "TRACE";

        default:
            return "Unknown";
    }
}

h2x_log_level string_to_h2x_log_level(char* log_level_string)
{
    if(strcasecmp(log_level_string, "OFF") == 0)
    {
        return H2X_LOG_LEVEL_OFF;
    }
    else if(strcasecmp(log_level_string, "FATAL") == 0)
    {
        return H2X_LOG_LEVEL_FATAL;
    }
    else if(strcasecmp(log_level_string, "ERROR") == 0)
    {
        return H2X_LOG_LEVEL_ERROR;
    }
    else if(strcasecmp(log_level_string, "WARN") == 0)
    {
        return H2X_LOG_LEVEL_WARN;
    }
    else if(strcasecmp(log_level_string, "INFO") == 0)
    {
        return H2X_LOG_LEVEL_INFO;
    }
    else if(strcasecmp(log_level_string, "DEBUG") == 0)
    {
        return H2X_LOG_LEVEL_DEBUG;
    }
    else if(strcasecmp(log_level_string, "TRACE") == 0)
    {
        return H2X_LOG_LEVEL_TRACE;
    }

    return H2X_LOG_LEVEL_OFF;
}

h2x_log_dest string_to_h2x_log_dest(char* log_dest_string)
{
    if(strcasecmp(log_dest_string, "NONE") == 0)
    {
        return H2X_LOG_DEST_NONE;
    }
    else if(strcasecmp(log_dest_string, "STDERR") == 0)
    {
        return H2X_LOG_DEST_STDERR;
    }
    else if(strcasecmp(log_dest_string, "FILE") == 0)
    {
        return H2X_LOG_DEST_FILE;
    }

    return H2X_LOG_DEST_NONE;
}


