
#ifndef H2X_ENUM_TYPES_H
#define H2X_ENUM_TYPES_H

typedef enum {
    H2X_MODE_NONE,
    H2X_MODE_CLIENT,
    H2X_MODE_SERVER
} h2x_mode;

typedef enum {
    H2X_SECURITY_NONE,
    H2X_SECURITY_TLS
} h2x_security_protocol_type;

typedef enum {
    H2X_NOT_ON_FRAME,
    H2X_ON_HEADER,
    H2X_HEADER_FILLED,
    H2X_ON_DATA
} h2x_connection_state;

typedef enum  {
    H2X_IDLE,
    H2X_RESERVED_LOCAL,
    H2X_RESERVED_REMOTE,
    H2X_OPEN,
    H2X_HALF_CLOSED_LOCAL,
    H2X_HALF_CLOSED_REMOTE,
    H2X_CLOSED
} h2x_stream_state;

typedef enum {
    H2X_STREAM_INBOUND,
    H2X_STREAM_OUTBOUND,
} h2x_stream_push_dir;

typedef enum {
    H2X_NO_ERROR = 0x00,
    H2X_PROTOCOL_ERROR = 0x01,
    H2X_STREAM_CLOSED = 0x05
} h2x_connection_error;

typedef enum {
    H2X_DATA = 0x00,
    H2X_HEADERS = 0x01,
    H2X_PRIORITY = 0x02,
    H2X_RST_STREAM = 0x03,
    H2X_SETTINGS = 0x04,
    H2X_PUSH_PROMISE = 0x05,
    H2X_PING = 0x06,
    H2X_GOAWAY = 0x07,
    H2X_WINDOW_UPDATE = 0x08,
    H2X_CONTINUATION = 0x09
} h2x_frame_type;

typedef enum {
    H2X_END_STREAM = 0x01,
    H2X_END_HEADERS = 0x04,
    H2X_PADDED = 0x08,
    H2X_PRIORITY_FLAG = 0x20
} h2x_frame_flags;

typedef enum {
    H2X_ICT_PENDING_READ,
    H2X_ICT_PENDING_WRITE,
    H2X_ICT_PENDING_CLOSE,
    H2X_ICT_COUNT
} h2x_intrusive_chain_type;

typedef enum {
    H2X_LOG_LEVEL_OFF = 0,
    H2X_LOG_LEVEL_FATAL = 1,
    H2X_LOG_LEVEL_ERROR = 2,
    H2X_LOG_LEVEL_WARN = 3,
    H2X_LOG_LEVEL_INFO = 4,
    H2X_LOG_LEVEL_DEBUG = 5,
    H2X_LOG_LEVEL_TRACE = 6
} h2x_log_level;

typedef enum {
    H2X_LOG_DEST_NONE,
    H2X_LOG_DEST_STDERR,
    H2X_LOG_DEST_FILE
} h2x_log_dest;

char* h2x_log_level_to_string(h2x_log_level log_level);
h2x_log_level string_to_h2x_log_level(char* log_level_string);
h2x_log_dest string_to_h2x_log_dest(char* log_dest_string);

char* h2x_stream_state_to_string(h2x_stream_state state);
char* h2x_intrusive_chain_type_to_string(h2x_intrusive_chain_type chain_type);
char* h2x_frame_type_to_string(h2x_frame_type frame_type);

#endif // H2X_ENUM_TYPES_H
