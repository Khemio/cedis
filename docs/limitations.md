# Known limitations and improvement priorities

> Review of [`Khemio/cedis`](https://github.com/Khemio/cedis) `main` as inspected on 2026-11-13. This is a documentation-oriented source review, **not** a completed sanitizer run, test audit, or security assessment.

Cedis is an educational Redis-inspired server. It should only be used in a controlled, local learning environment until the issues below are addressed. They are listed to make the current state transparent and to guide development.

## Priority 0 — memory safety and basic execution

| Area | Observation in the source | Recommended change |
| --- | --- | --- |
| Replication metadata allocation | `initMaster`/`initSlave` allocate `sizeof(Info)` (a pointer typedef) rather than the size of the pointed-to `struct Info`. | Allocate `sizeof(*info)`, check results, and add a corresponding destructor. |
| RDB payload conversion | `hexToBinary` offsets the pointer returned by `malloc` by one, then indexes through the requested length. | Keep the original allocation pointer, allocate sufficient bytes, and remove out-of-bounds writes. |
| Request parser ownership | Token buffers and per-request argument arrays are not consistently released; tokenization and freeing logic are unsafe. | Use a length-aware parser with explicit ownership and a request cleanup routine. |
| Reply formatting | Several commands use fixed-size `sprintf` buffers; `INFO` allocates/reallocates insufficient space for formatted content. | Calculate required lengths, use bounded formatting, and validate response sizes. |
| Connection lifecycle | The client worker never exits its receive loop on `recv() == 0` or `recv() < 0`. | Handle EOF/errors, release thread arguments and requests, close sockets, and decide whether to detach/join threads. |
| Replica initialization | The `--replicaof` branch calls `acceptCon(server_sock)` before `server_sock` is initialized. | Fix startup ordering and separately establish the upstream replication connection. |

## Priority 1 — correctness and protocol behavior

| Area | Observation in the source | Recommended change |
| --- | --- | --- |
| Duplicate keys | `DictInsert` always prepends a new entry, so `SET` does not replace an existing value. | Update an existing key or remove its previous entry before inserting. |
| Dictionary size | `DictDelete` removes an entry but does not decrement `d->n`. | Maintain accurate entry counts and add invariant tests. |
| Expiration | Epoch milliseconds returned as `long` are narrowed to `int`; the helper uses `CLOCK_REALTIME`. | Use consistent 64-bit timestamp types and define time source/expiration rules. |
| Request framing | One `recv()` is treated as one request; RESP arrays with 10+ elements are miscounted. | Buffer bytes across reads and parse length-prefixed RESP frames incrementally. |
| Payload integrity | The parser lowercases **all** arguments, including keys and values. | Case-fold only command names; preserve payload bytes exactly. |
| Shared state | Multiple pthreads read/write the same dictionary with no synchronization. | Introduce synchronization or switch to a single-threaded event loop; test with concurrent clients. |
| Network writes | The code generally assumes one `send()` transmits the full response. | Handle partial writes, interruptions, and socket errors. |

## Priority 2 — feature completeness and maintainability

| Area | Observation in the source | Recommended change |
| --- | --- | --- |
| Replication | Handshake, snapshot, replica-list tracking, and `SET` forwarding are partial; offsets and continuous synchronization are absent. | Treat replication as experimental until protocol and consistency tests pass. |
| Persistence | The server only holds data in memory. An empty RDB-like payload is used for the experimental handshake, but persistence is not implemented. | Add real serialization and recovery only if persistence becomes a project goal. |
| Tests | The repository has a file of manual `nc` examples but no committed automated test suite. | Add dictionary unit tests, parser fixtures, socket integration tests, and sanitizers. |
| Portability | Build and networking code depend on POSIX APIs and the Makefile emits `server.exe`. | Document Linux/WSL support; optionally normalize output names and make flags. |
| Reliability | Error handling is often incomplete or diagnostic-only. | Check all allocations, `pthread_create`, `bind`, `listen`, `accept`, `recv`, and `send` paths. |

## Suggested verification checklist

Before calling the server more than a local prototype, verify these behaviors with repeatable tests:

- `SET` overwrites the exact same key and never reveals older values later.
- Key/value bytes are preserved, including mixed case, spaces, empty strings, and binary payloads if supported.
- Requests split across multiple TCP packets and multiple requests received together are parsed correctly.
- Connection shutdown, malformed frames, and out-of-range lengths do not crash or spin the server.
- Expiration behaves consistently across clock changes and long-running processes.
- Multiple clients can read/write without data races or hash-table corruption.
- Memory checking (for example, AddressSanitizer and UndefinedBehaviorSanitizer) passes representative tests.
- Replication works through handshake, state transfer, subsequent writes, disconnects, and reconnects before it is advertised as a supported feature.

## Recommended implementation order

**Phase 1: stabilize the core.** Fix unsafe allocations and lifetimes, client disconnect handling, incremental RESP parsing, and correct `SET` semantics.

**Phase 2: validate the storage engine.** Repair TTL math, add dictionary/parser tests, ensure accurate accounting, and choose thread safety semantics.

**Phase 3: build confidence.** Add sanitizers and end-to-end tests, then implement and verify replication deliberately rather than building on the current proof-of-concept path.

This list is intentionally specific about observed code paths. It is not a claim that these are the only defects or that fixes have already been applied.