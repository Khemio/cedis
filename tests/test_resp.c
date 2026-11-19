#include "../utils/resp.h"
#include "../utils/resp_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // A request containing an embedded null byte.
    const unsigned char cmd[] =
        "*2\r\n$4\r\nECHO\r\n$5\r\nHe\0Lo\r\n";

    struct resp_request req = {0};
    size_t consumed = 0;

    enum resp_parse_status status =
        resp_parse_request(
            cmd, sizeof(cmd) - 1, &req, &consumed
        );

    assert(status == RESP_OK);
    assert(consumed == sizeof(cmd) - 1);
    assert(req.argc == 2);

    assert(req.args[0].len == 4);
    assert(memcmp(req.args[0].data, "ECHO", 4) == 0);

    assert(req.args[1].len == 5);
    assert(memcmp(req.args[1].data, "He\0Lo", 5) == 0);

    resp_request_free(&req);

    // A partial message must not be considered invalid.
    status = resp_parse_request(
        cmd, 6, &req, &consumed
    );

    assert(status == RESP_INCOMPLETE);
    assert(req.args == NULL);
    assert(consumed == 0);

    // A malformed bulk-string terminator.
    const unsigned char invalid[] =
        "*1\r\n$1\r\nX!\n";

    status = resp_parse_request(
        invalid, sizeof(invalid) - 1,
        &req, &consumed
    );

    assert(status == RESP_INVALID);


    // Test 1: Empty bulk string
    const unsigned char empty[] =
        "*2\r\n$4\r\nECHO\r\n$0\r\n\r\n";

    status = resp_parse_request(
        empty, sizeof(empty) - 1, &req, &consumed
    );

    assert(status == RESP_OK);
    assert(req.argc == 2);
    assert(req.args[1].len == 0);
    assert(consumed == sizeof(empty) - 1);
    resp_request_free(&req);

    // Test 2: Two concatenated requests
    const unsigned char combined[] =
        "*1\r\n$4\r\nPING\r\n"
        "*1\r\n$4\r\nPING\r\n";

    const size_t combined_len = sizeof(combined) - 1;

    status = resp_parse_request(
        combined, combined_len, &req, &consumed
    );

    assert(status == RESP_OK);
    assert(consumed == combined_len / 2);
    resp_request_free(&req);

    size_t first_consumed = consumed;

    status = resp_parse_request(
        combined + first_consumed,
        combined_len - first_consumed,
        &req,
        &consumed
    );

    assert(status == RESP_OK);
    assert(consumed == first_consumed);
    resp_request_free(&req);

    // Test 3: Incomplete bulk-string payload
    const unsigned char partial[] =
        "*1\r\n$5\r\nabc";

    status = resp_parse_request(
        partial, sizeof(partial) - 1,
        &req, &consumed
    );

    assert(status == RESP_INCOMPLETE);
    assert(consumed == 0);
    assert(req.args == NULL);

    // Test 4: Reject excessive argument counts
    const unsigned char excessive[] =
        "*1025\r\n";

    status = resp_parse_request(
        excessive, sizeof(excessive) - 1,
        &req, &consumed
    );

    assert(status == RESP_INVALID);
    assert(req.args == NULL);


    struct resp_stream stream = {0};

    // First TCP chunk contains an incomplete PING.
    const unsigned char chunk1[] =
        "*1\r\n$4\r\nPI";

    // Second chunk completes it and contains another PING.
    const unsigned char chunk2[] =
        "NG\r\n*1\r\n$4\r\nPING\r\n";

    assert(resp_stream_append(
        &stream, chunk1, sizeof(chunk1) - 1
    ));

    status = resp_parse_request(
        stream.data, stream.len, &req, &consumed
    );

    assert(status == RESP_INCOMPLETE);

    assert(resp_stream_append(
        &stream, chunk2, sizeof(chunk2) - 1
    ));

    // Parse the first complete request.
    status = resp_parse_request(
        stream.data, stream.len, &req, &consumed
    );

    assert(status == RESP_OK);
    assert(consumed == 14);
    resp_request_free(&req);

    assert(resp_stream_consume(&stream, consumed));

    // Parse the second request from the remaining bytes.
    status = resp_parse_request(
        stream.data, stream.len, &req, &consumed
    );

    assert(status == RESP_OK);
    assert(consumed == 14);
    resp_request_free(&req);

    assert(resp_stream_consume(&stream, consumed));
    assert(stream.len == 0);

    resp_stream_free(&stream);


    puts("All parser tests passed.");
    return 0;
}
