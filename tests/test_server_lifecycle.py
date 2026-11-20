
import signal
import socket
import subprocess
import time

SERVER = "./cedis-dev"
HOST = "127.0.0.1"

PING = b"*1\r\n$4\r\nPING\r\n"
PONG = b"+PONG\r\n"


def available_port():
    with socket.socket() as sock:
        sock.bind((HOST, 0))
        return sock.getsockname()[1]


def wait_until_ready(process, port):
    deadline = time.monotonic() + 10

    while time.monotonic() < deadline:
        if process.poll() is not None:
            output, _ = process.communicate()
            raise AssertionError(
                f"Server exited during startup:\n{output}"
            )

        try:
            return socket.create_connection(
                (HOST, port), timeout=0.2
            )
        except OSError:
            time.sleep(0.02)

    raise AssertionError("Server did not start")


def check_ping(port):
    with socket.create_connection(
        (HOST, port), timeout=3
    ) as sock:
        sock.sendall(PING)

        response = b""

        while len(response) < len(PONG):
            chunk = sock.recv(len(PONG) - len(response))
            assert chunk, "Unexpected disconnect"
            response += chunk

        assert response == PONG


def test_shutdown(signum):
    port = available_port()

    process = subprocess.Popen(
        [SERVER, "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    idle_client = None

    try:
        # Keep one client connected throughout shutdown.
        idle_client = wait_until_ready(process, port)

        check_ping(port)

        # A second server must fail to bind this port.
        second = subprocess.run(
            [SERVER, "--port", str(port)],
            capture_output=True,
            text=True,
            timeout=5,
        )

        assert second.returncode != 0
        assert "Address already in use" in second.stderr

        # The first server must still work.
        check_ping(port)

        # Request graceful shutdown.
        process.send_signal(signum)

        output, _ = process.communicate(timeout=10)

        assert process.returncode == 0, output
        assert "Stopping server..." in output
        assert "Server stopped." in output
        assert "LeakSanitizer" not in output, output

        print(f"{signal.Signals(signum).name} shutdown passed")

    finally:
        if idle_client is not None:
            idle_client.close()

        if process.poll() is None:
            process.kill()
            process.communicate()


if __name__ == "__main__":
    test_shutdown(signal.SIGINT)
    test_shutdown(signal.SIGTERM)

    print("All lifecycle tests passed.")
