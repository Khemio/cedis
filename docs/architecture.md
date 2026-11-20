
# Cedis

Cedis is an educational, Redis-inspired in-memory key-value server
written in C. It implements a subset of the Redis Serialization
Protocol (RESP2) over TCP, with custom storage, expiration, and
concurrent client handling.

The project began as an implementation of the CodeCrafters Redis
challenge and has since been refactored to emphasize correctness,
memory ownership, networking, and concurrency.

Cedis is not a production-ready database or a drop-in Redis
replacement.

## Features

- Binary-safe keys and values
- Incremental RESP2 request parsing
- TCP fragmentation and request pipelining
- Custom hash table using FNV-1a hashing and separate chaining
- Key replacement and dynamic hash-table resizing
- Millisecond TTL support with lazy expiration
- Concurrent clients using POSIX threads
- Mutex-protected shared key-value storage
- Graceful shutdown through SIGINT and SIGTERM
- Automated unit, integration, and lifecycle tests

### Supported commands

| Command | Behavior |
| --- | --- |
| `PING` | Returns `PONG` |
| `ECHO message` | Returns the message as a bulk string |
| `SET key value` | Creates or replaces a key |
| `SET key value PX milliseconds` | Sets a key with an expiration time |
| `GET key` | Returns the value or a null bulk string |

Command names are case-insensitive. Keys and values retain their
original bytes.

## Requirements

- Linux or a compatible POSIX environment (tested in WSL2)
- GCC
- GNU Make
- Python 3 for integration tests
- Optional: `redis-cli`

## Build

From the repository root:

```sh
make
```

This produces `cedis-dev`.

For a build with stricter compiler diagnostics:

```sh
make clean
make CFLAGS="-std=gnu11 -Wall -Wextra -Wpedantic -Werror -g -O0"
```

A compatibility build target is also available:

```sh
make server
```

This produces `server.exe`.

## Run

Start the server on the default port, 6379:

```sh
./cedis-dev
```

Or specify a port:

```sh
./cedis-dev --port 6381
```

In another terminal:

```sh
redis-cli -p 6381 PING
redis-cli -p 6381 ECHO hello
redis-cli -p 6381 SET greeting world
redis-cli -p 6381 GET greeting
redis-cli -p 6381 SET temporary value PX 2000
redis-cli -p 6381 GET temporary
```

Press Ctrl+C to request graceful shutdown.

The stable build does not support replication. Passing `--replicaof`
produces an explicit error.

## Architecture

```text
                   TCP clients
                       |
                       v
               Listening socket
                       |
                  pselect()
                       |
                    accept()
                       |
               Client worker thread
                       |
                 RESP reader
                       |
                RESP2 parser
                       |
               Command dispatcher
                       |
                Shared KV store
                       |
             Mutex-protected buckets
```

### Protocol handling

Each client has a receive buffer. The RESP reader accumulates bytes
from the socket and parses complete requests.

The parser supports arrays of bulk-string arguments. It handles
fragmented requests, multiple pipelined requests, and binary payloads.

### Storage

The custom key-value store uses FNV-1a hashing, separate chaining,
and dynamic resizing.

Keys and values are stored with explicit lengths. Setting an existing
key replaces its value.

The store uses a mutex to synchronize concurrent operations.

### Expiration

`SET ... PX` records an expiration deadline using a monotonic clock.

Expiration is lazy: expired entries are detected and removed when
accessed. There is no background expiration worker.

### Concurrency and shutdown

Cedis uses a detached POSIX thread for each client.

The main thread maintains a registry of active clients. During
shutdown, it interrupts their socket operations and waits for
workers to release their resources before destroying shared storage.

Signal handling and `pselect()` coordinate server termination
without relying on an indefinitely blocked `accept()` call.

## Tests

Run unit and lifecycle tests:

```sh
make test
```

The test suite covers RESP parsing, socket readers, key-value storage,
concurrent dictionary operations, and graceful process shutdown.

Additional integration tests exercise a running server on port 6381.

First start:

```sh
./cedis-dev --port 6381
```

Then, in another terminal:

```sh
python3 tests/test_server_smoke.py
python3 tests/test_server_integration.py
python3 tests/test_connections.py
python3 tests/test_server_errors.py
```

These cover request fragmentation, pipelining, binary data, expiration,
concurrent requests, malformed input, and repeated client connections.

### Sanitizers

Build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make clean
make SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
make SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" test
```

The core tests and lifecycle tests have been exercised with these
sanitizers. This does not constitute a guarantee of memory safety or
freedom from concurrency defects.

## Limitations

- Only a small subset of Redis commands is implemented.
- Request parsing supports flat RESP2 arrays of bulk strings, not
  the complete RESP2 or RESP3 protocols.
- Storage is entirely in memory; no persistence is implemented.
- Expiration is lazy rather than actively scheduled.
- The thread-per-client architecture does not impose a strict
  concurrency limit.
- Replication, transactions, authentication, clustering, and
  pub/sub are not supported in the stable build.
- The server is intended for local experimentation, not deployment
  on untrusted networks.

## Project history

Cedis originated in the CodeCrafters Redis challenge. Earlier
experiments included replica handshake and synchronization logic.

That unfinished work is preserved in the
`codecrafters/replication-baseline` branch for future development.

The stable implementation focuses on a smaller, tested core.

## Documentation

- [Architecture](docs/architecture.md)
- [Known limitations](docs/limitations.md)
- [Development roadmap](docs/roadmap.md)

## Acknowledgment

The original project direction comes from the
[CodeCrafters Redis challenge](https://codecrafters.io/challenges/redis).

Redis is a separate project. Cedis is an independent educational
implementation.
