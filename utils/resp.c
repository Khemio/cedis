
#include "resp.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 1024
#define MAX_BULK_LEN (1024 * 1024)

void resp_request_free(struct resp_request *req) {
    if (req == NULL) return;

    for (size_t i = 0; i < req->argc; i++) {
        free(req->args[i].data);
    }

    free(req->args);
    req->args = NULL;
    req->argc = 0;
}

// Parse "*2\r\n" or "$5\r\n" beginning at *pos.
static enum resp_parse_status parse_length(
    const unsigned char *buffer,
    size_t buffer_len,
    size_t *pos,
    unsigned char prefix,
    size_t *value
) {
    size_t i = *pos;

    if (i >= buffer_len) return RESP_INCOMPLETE;
    if (buffer[i++] != prefix) return RESP_INVALID;

    size_t number = 0;
    size_t digits = 0;

    while (i < buffer_len) {
        unsigned char ch = buffer[i++];

        if (ch >= '0' && ch <= '9') {
            size_t digit = (size_t)(ch - '0');

            if (number > (SIZE_MAX - digit) / 10)
                return RESP_INVALID;

            number = number * 10 + digit;
            digits++;
        } else if (ch == '\r') {
            if (digits == 0) return RESP_INVALID;
            if (i >= buffer_len) return RESP_INCOMPLETE;
            if (buffer[i++] != '\n') return RESP_INVALID;

            *pos = i;
            *value = number;
            return RESP_OK;
        } else {
            return RESP_INVALID;
        }
    }

    return RESP_INCOMPLETE;
}

enum resp_parse_status resp_parse_request(
    const unsigned char *buffer,
    size_t buffer_len,
    struct resp_request *out,
    size_t *consumed
) {
    if (out == NULL || consumed == NULL)
        return RESP_INVALID;

    *out = (struct resp_request){0};
    *consumed = 0;

    if (buffer == NULL)
        return buffer_len == 0
            ? RESP_INCOMPLETE : RESP_INVALID;

    size_t pos = 0;
    size_t argc = 0;

    enum resp_parse_status status =
        parse_length(buffer, buffer_len, &pos, '*', &argc);

    if (status != RESP_OK) return status;
    if (argc == 0 || argc > MAX_ARGS) return RESP_INVALID;

    struct resp_request req = {0};

    req.args = calloc(argc, sizeof *req.args);
    if (req.args == NULL) return RESP_NO_MEMORY;

    req.argc = argc;

    for (size_t i = 0; i < argc; i++) {
        size_t arg_len = 0;

        status = parse_length(
            buffer, buffer_len, &pos, '$', &arg_len
        );

        if (status != RESP_OK) goto fail;

        if (arg_len > MAX_BULK_LEN) {
            status = RESP_INVALID;
            goto fail;
        }

        size_t remaining = buffer_len - pos;

        if (arg_len > remaining ||
            remaining - arg_len < 2) {
            status = RESP_INCOMPLETE;
            goto fail;
        }

        if (buffer[pos + arg_len] != '\r' ||
            buffer[pos + arg_len + 1] != '\n') {
            status = RESP_INVALID;
            goto fail;
        }

        req.args[i].data =
            malloc(arg_len == 0 ? 1 : arg_len);

        if (req.args[i].data == NULL) {
            status = RESP_NO_MEMORY;
            goto fail;
        }

        memcpy(req.args[i].data, buffer + pos, arg_len);
        req.args[i].len = arg_len;

        pos += arg_len + 2;
    }

    *out = req;
    *consumed = pos;
    return RESP_OK;

fail:
    resp_request_free(&req);
    return status;
}
