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

char* h2x_stream_state_to_string(h2x_stream_state state)
{
    switch(state)
    {
        case H2X_IDLE:
            return "Idle";

        case H2X_RESERVED_LOCAL:
            return "Reserved(Local)";

        case H2X_RESERVED_REMOTE:
            return "Reserved(Remote)";

        case H2X_OPEN:
            return "Open";

        case H2X_HALF_CLOSED_LOCAL:
            return "HalfClosed(Local)";

        case H2X_HALF_CLOSED_REMOTE:
            return "HalfClosed(Remote)";

        case H2X_CLOSED:
            return "Closed";

        default:
            return "OMG I don't know";
    }
}

char* h2x_intrusive_chain_type_to_string(h2x_intrusive_chain_type chain_type)
{
    switch(chain_type)
    {
        case H2X_ICT_PENDING_READ:
            return "PendingRead";

        case H2X_ICT_PENDING_WRITE:
            return "PendingWrite";

        case H2X_ICT_PENDING_CLOSE:
            return "PendingClose";

        default:
            return "Invalid";
    }
}

char* h2x_frame_type_to_string(h2x_frame_type frame_type)
{
    switch(frame_type)
    {
        case H2X_DATA:
            return "Data";

        case H2X_HEADERS:
            return "Headers";

        case H2X_PRIORITY:
            return "Priority";

        case H2X_RST_STREAM:
            return "RstStream";

        case H2X_SETTINGS:
            return "Settings";

        case H2X_PUSH_PROMISE:
            return "PushPromise";

        case H2X_PING:
            return "Ping";

        case H2X_GOAWAY:
            return "GoAway";

        case H2X_WINDOW_UPDATE:
            return "WindowUpdate";

        case H2X_CONTINUATION:
            return "Continuation";

        default:
            return "Dunno";
    }
}

