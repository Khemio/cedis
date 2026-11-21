
import socket

import os

PORT = int(os.environ.get("CEDIS_TEST_PORT", "6381"))
ADDRESS = ("127.0.0.1", PORT)

PING = b"*1\r\n$4\r\nPING\r\n"
PONG = b"+PONG\r\n"


def expect_disconnect(payload, half_close=False):
    with socket.create_connection(ADDRESS, timeout=3) as sock:
        sock.sendall(payload)

        if half_close:
            sock.shutdown(socket.SHUT_WR)

        sock.settimeout(3)

        # The server should close an invalid connection.
        assert sock.recv(1) == b""


def test_invalid_request():
    # Wrong bulk-string terminator.
    expect_disconnect(b"*1\r\n$4\r\nPING\rX")

    # Empty command array.
    expect_disconnect(b"*0\r\n")

    print("Invalid request handling passed")


def test_truncated_request():
    # Client stops sending before the command is complete.
    expect_disconnect(
        b"*1\r\n$4\r\nPI",
        half_close=True
    )

    print("Truncated request handling passed")


def test_valid_then_invalid():
    with socket.create_connection(ADDRESS, timeout=3) as sock:
        sock.sendall(PING + b"*0\r\n")
        sock.settimeout(3)

        received = b""

        while len(received) < len(PONG):
            chunk = sock.recv(len(PONG) - len(received))
            assert chunk
            received += chunk

        assert received == PONG
        assert sock.recv(1) == b""

    print("Valid-then-invalid pipeline passed")


def test_server_survives():
    with socket.create_connection(ADDRESS, timeout=3) as sock:
        sock.sendall(PING)
        assert sock.recv(1024) == PONG

    print("Server remains operational")


if __name__ == "__main__":
    test_invalid_request()
    test_truncated_request()
    test_valid_then_invalid()
    test_server_survives()

    print("All error-handling tests passed.")
