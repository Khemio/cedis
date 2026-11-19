
import socket

with socket.create_connection(("127.0.0.1", 6381)) as sock:
    # Two commands sent together.
    request = b"*1\r\n$4\r\nPING\r\n" * 2

    sock.sendall(request)

    expected = b"+PONG\r\n+PONG\r\n"
    received = b""

    while len(received) < len(expected):
        chunk = sock.recv(1024)
        if not chunk:
            raise RuntimeError("Server disconnected")
        received += chunk

    assert received == expected
    print("Pipelining passed")

with socket.create_connection(("127.0.0.1", 6381)) as sock:
    # Deliberately split a command.
    sock.sendall(b"*1\r\n$4\r\nPI")
    sock.sendall(b"NG\r\n")

    assert sock.recv(1024) == b"+PONG\r\n"
    print("Fragmented request passed")
