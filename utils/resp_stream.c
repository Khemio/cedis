
#include "resp_stream.h"

#include <stdlib.h>
#include <string.h>

bool resp_stream_append(
    struct resp_stream *stream,
    const unsigned char *data,
    size_t len
) {
    if (stream == NULL || (data == NULL && len != 0))
        return false;

    // Check before addition to avoid size_t overflow.
    if (stream->len > RESP_STREAM_MAX_BYTES ||
        len > RESP_STREAM_MAX_BYTES - stream->len)
        return false;

    size_t required = stream->len + len;

    if (required > stream->capacity) {
        size_t new_capacity =
            stream->capacity == 0 ? 256 : stream->capacity;

        while (new_capacity < required) {
            new_capacity *= 2;
        }

        unsigned char *new_data =
            realloc(stream->data, new_capacity);

        if (new_data == NULL)
            return false;

        stream->data = new_data;
        stream->capacity = new_capacity;
    }

    if (len != 0)
        memcpy(stream->data + stream->len, data, len);

    stream->len = required;
    return true;
}

bool resp_stream_consume(
    struct resp_stream *stream,
    size_t consumed
) {
    if (stream == NULL || consumed > stream->len)
        return false;

    size_t remaining = stream->len - consumed;

    if (remaining != 0) {
        memmove(
            stream->data,
            stream->data + consumed,
            remaining
        );
    }

    stream->len = remaining;
    return true;
}

void resp_stream_free(struct resp_stream *stream) {
    if (stream == NULL)
        return;

    free(stream->data);
    *stream = (struct resp_stream){0};
}
