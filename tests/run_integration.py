
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "cedis-dev"

TESTS = [
    "test_server_smoke.py",
    "test_server_integration.py",
    "test_connections.py",
    "test_server_errors.py",
]

PING = b"*1\r\n$4\r\nPING\r\n"
PONG = b"+PONG\r\n"


def available_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_until_ready(process, port):
    deadline = time.monotonic() + 10

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                "Cedis exited before becoming ready"
            )

        try:
            with socket.create_connection(
                ("127.0.0.1", port), timeout=0.5
            ) as sock:
                sock.sendall(PING)

                with sock.makefile("rb") as stream:
                    if stream.read(len(PONG)) == PONG:
                        return

        except OSError:
            time.sleep(0.05)

    raise RuntimeError("Cedis startup timed out")


def main():
    port = available_port()

    env = os.environ.copy()
    env["CEDIS_TEST_PORT"] = str(port)

    print(f"Starting Cedis on port {port}", flush=True)

    # Use a file to avoid blocking on a full output pipe.
    with tempfile.TemporaryFile(mode="w+t") as log:
        process = subprocess.Popen(
            [str(SERVER), "--port", str(port)],
            cwd=ROOT,
            stdout=log,
            stderr=subprocess.STDOUT,
        )

        passed = False

        try:
            wait_until_ready(process, port)

            for test in TESTS:
                print(f"\nRunning {test}", flush=True)

                subprocess.run(
                    [sys.executable, str(ROOT / "tests" / test)],
                    cwd=ROOT,
                    env=env,
                    check=True,
                    timeout=120,
                )

            passed = True

        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)

            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

            if not passed or process.returncode != 0:
                log.seek(0)
                print(
                    "\nCedis server log:\n" + log.read(),
                    file=sys.stderr,
                )

        if process.returncode != 0:
            raise RuntimeError(
                f"Cedis exited with status {process.returncode}"
            )

    print("\nAll TCP integration suites passed.")


if __name__ == "__main__":
    main()
