
#include "resp_reader.h"

#include <errno.h>
#include <sys/socket.h>

enum resp_read_status resp_reader_next(
    struct resp_reader *reader,
    struct resp_request *out
) {
    if (reader == NULL || out == NULL)
        return RESP_READ_INVALID;

    *out = (struct resp_request){0};

    for (;;) {
        size_t consumed = 0;

        enum resp_parse_status status = resp_parse_request(
            reader->stream.data,
            reader->stream.len,
            out,
            &consumed
        );

        if (status == RESP_OK) {
            if (!resp_stream_consume(
                    &reader->stream, consumed)) {
                resp_request_free(out);
                return RESP_READ_INVALID;
            }

            return RESP_READ_OK;
        }

        if (status == RESP_INVALID)
            return RESP_READ_INVALID;

        if (status == RESP_NO_MEMORY)
            return RESP_READ_NO_MEMORY;

        // The current buffer contains an incomplete request.
        // Read more bytes from the socket.
        unsigned char chunk[4096];

        ssize_t received = recv(
            reader->fd, chunk, sizeof chunk, 0
        );

        if (received > 0) {
            if (!resp_stream_append(
                    &reader->stream,
                    chunk,
                    (size_t)received)) {
                return RESP_READ_BUFFER_ERROR;
            }

            continue;
        }

        if (received == 0) {
            return reader->stream.len == 0
                ? RESP_READ_CLOSED
                : RESP_READ_TRUNCATED;
        }

        if (errno == EINTR)
            continue;

        return RESP_READ_IO_ERROR;
    }
}

void resp_reader_free(struct resp_reader *reader) {
    if (reader == NULL)
        return;

    resp_stream_free(&reader->stream);
}
