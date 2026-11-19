
#include "../utils/resp_reader.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int sockets[2];

    assert(socketpair(
        AF_UNIX, SOCK_STREAM, 0, sockets
    ) == 0);

    struct resp_reader reader = {
        .fd = sockets[1]
    };

    const char commands[] =
        "*1\r\n$4\r\nPING\r\n"
        "*1\r\n$4\r\nPING\r\n";

    ssize_t sent = send(
        sockets[0],
        commands,
        sizeof(commands) - 1,
        0
    );

    assert(sent == (ssize_t)(sizeof(commands) - 1));

    // Finish the sending side.
    assert(shutdown(sockets[0], SHUT_WR) == 0);

    for (int i = 0; i < 2; i++) {
        struct resp_request req = {0};

        enum resp_read_status status =
            resp_reader_next(&reader, &req);

        assert(status == RESP_READ_OK);
        assert(req.argc == 1);
        assert(req.args[0].len == 4);
        assert(memcmp(req.args[0].data, "PING", 4) == 0);

        resp_request_free(&req);
    }

    struct resp_request req = {0};

    assert(resp_reader_next(&reader, &req)
           == RESP_READ_CLOSED);

    resp_reader_free(&reader);
    close(sockets[0]);
    close(sockets[1]);

    puts("All reader tests passed.");
    return 0;
}
