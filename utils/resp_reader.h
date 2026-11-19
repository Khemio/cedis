
#ifndef CEDIS_RESP_READER_H
#define CEDIS_RESP_READER_H

#include "resp.h"
#include "resp_stream.h"

enum resp_read_status {
    RESP_READ_OK,
    RESP_READ_CLOSED,
    RESP_READ_TRUNCATED,
    RESP_READ_INVALID,
    RESP_READ_NO_MEMORY,
    RESP_READ_BUFFER_ERROR,
    RESP_READ_IO_ERROR
};

struct resp_reader {
    int fd;
    struct resp_stream stream;
};

enum resp_read_status resp_reader_next(
    struct resp_reader *reader,
    struct resp_request *out
);

void resp_reader_free(struct resp_reader *reader);

#endif
