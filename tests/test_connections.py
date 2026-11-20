
import socket
from concurrent.futures import ThreadPoolExecutor

ADDRESS = ("127.0.0.1", 6381)
PING = b"*1\r\n$4\r\nPING\r\n"
REPLY = b"+PONG\r\n"


def connect_and_ping(_):
    with socket.create_connection(ADDRESS, timeout=5) as sock:
        sock.sendall(PING)

        response = b""
        while len(response) < len(REPLY):
            chunk = sock.recv(64)
            if not chunk:
                raise AssertionError("Unexpected disconnect")
            response += chunk

        assert response == REPLY


if __name__ == "__main__":
    # Many short-lived connections, with some overlap.
    with ThreadPoolExecutor(max_workers=20) as pool:
        list(pool.map(connect_and_ping, range(1000)))

    print("1000 client connections passed")
