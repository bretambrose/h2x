
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
    H2X_STREAM_NOT_INIT,
    H2X_STREAM_INBOUND,
    H2X_STREAM_OUTBOUND,
} h2x_stream_push_dir;

typedef enum {
    H2X_STREAM_NO_ERROR = 0x00,
    H2X_STREAM_PROTOCOL_ERROR = 0x01,
    H2X_STREAM_CLOSED = 0x05
} h2x_stream_error;

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
    H2X_PADDED = 0x08
} h2x_frame_flags;

#endif // H2X_ENUM_TYPES_H
