
import socket
import time
from concurrent.futures import ThreadPoolExecutor

ADDRESS = ("127.0.0.1", 6381)


def command(*args: bytes) -> bytes:
    result = b"*" + str(len(args)).encode() + b"\r\n"

    for arg in args:
        result += b"$" + str(len(arg)).encode() + b"\r\n"
        result += arg + b"\r\n"

    return result


def read_reply(stream):
    prefix = stream.read(1)

    if prefix in (b"+", b"-"):
        line = stream.readline()
        assert line.endswith(b"\r\n")
        return prefix + line[:-2]

    if prefix == b"$":
        line = stream.readline()
        assert line.endswith(b"\r\n")
        length = int(line[:-2])

        if length == -1:
            return None

        data = stream.read(length)
        assert len(data) == length
        assert stream.read(2) == b"\r\n"
        return data

    raise AssertionError(f"Unexpected RESP prefix: {prefix!r}")


def test_binary_data():
    with socket.create_connection(ADDRESS, timeout=10) as sock:
        with sock.makefile("rb") as stream:
            key = b"user:\x00\xff"
            value = b"A\x00B\xffC"

            # Pipeline SET and GET.
            sock.sendall(
                command(b"SET", key, value)
                + command(b"GET", key)
            )

            assert read_reply(stream) == b"+OK"
            assert read_reply(stream) == value

            # ECHO must preserve arbitrary bytes.
            sock.sendall(command(b"eChO", value))
            assert read_reply(stream) == value

            # Empty keys and values are valid.
            sock.sendall(
                command(b"SET", b"", b"")
                + command(b"GET", b"")
            )

            assert read_reply(stream) == b"+OK"
            assert read_reply(stream) == b""

    print("Binary data and pipelining passed")


def test_expiration():
    with socket.create_connection(ADDRESS, timeout=10) as sock:
        with sock.makefile("rb") as stream:
            sock.sendall(
                command(
                    b"SET", b"ttl-test", b"hello",
                    b"PX", b"80"
                )
            )
            assert read_reply(stream) == b"+OK"

            time.sleep(0.15)

            sock.sendall(command(b"GET", b"ttl-test"))
            assert read_reply(stream) is None

            # An invalid expiration must not overwrite data.
            sock.sendall(
                command(b"SET", b"stable", b"original")
            )
            assert read_reply(stream) == b"+OK"

            sock.sendall(
                command(
                    b"SET", b"stable", b"changed",
                    b"PX", b"0"
                )
            )
            assert read_reply(stream).startswith(b"-ERR")

            sock.sendall(command(b"GET", b"stable"))
            assert read_reply(stream) == b"original"

    print("Expiration and invalid arguments passed")


def worker(worker_id: int):
    with socket.create_connection(ADDRESS, timeout=10) as sock:
        with sock.makefile("rb") as stream:
            for i in range(50):
                key = f"worker:{worker_id}:{i}".encode()
                value = f"value:{i}".encode() + b"\x00"

                sock.sendall(
                    command(b"SET", key, value)
                    + command(b"GET", key)
                )

                assert read_reply(stream) == b"+OK"
                assert read_reply(stream) == value


def test_concurrent_clients():
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(worker, range(8)))

    print("Concurrent clients passed")


if __name__ == "__main__":
    test_binary_data()
    test_expiration()
    test_concurrent_clients()

    print("All server integration tests passed.")
