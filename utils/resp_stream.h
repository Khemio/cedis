
#ifndef CEDIS_RESP_STREAM_H
#define CEDIS_RESP_STREAM_H

#include <stdbool.h>
#include <stddef.h>

#define RESP_STREAM_MAX_BYTES (2u * 1024u * 1024u)

struct resp_stream {
    unsigned char *data;
    size_t len;
    size_t capacity;
};

bool resp_stream_append(
    struct resp_stream *stream,
    const unsigned char *data,
    size_t len
);

bool resp_stream_consume(
    struct resp_stream *stream,
    size_t consumed
);

void resp_stream_free(struct resp_stream *stream);

#endif
