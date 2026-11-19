#ifndef CEDIS_RESP_H
#define CEDIS_RESP_H

#include <stddef.h>

struct resp_arg {
    unsigned char *data;
    size_t len;
};

struct resp_request {
    struct resp_arg *args;
    size_t argc;
};

enum resp_parse_status {
    RESP_OK,
    RESP_INCOMPLETE,
    RESP_INVALID,
    RESP_NO_MEMORY
};

enum resp_parse_status resp_parse_request(
    const unsigned char *buffer,
    size_t buffer_len,
    struct resp_request *out,
    size_t *consumed
);

void resp_request_free(struct resp_request *req);

#endif