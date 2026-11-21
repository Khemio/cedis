# Cedis: known limitations and remaining improvements

> **Scope:** stable server on `cleanup/cv-ready`, reviewed 2026-11-20. This replaces the 2026-11-13 review of the original implementation. Issues in the preserved `codecrafters/replication-baseline` branch are **not** automatically issues in the stable build.

Cedis is a small, educational Redis-inspired key-value server, not a drop-in Redis replacement or a hardened service. The supported command set is `PING`, `ECHO`, `SET` (including `PX`), and `GET`. Use it in a controlled local development environment.

## 1. Highest-priority remaining correctness and reliability work

| Area | Current observation | Suggested improvement |
| --- | --- | --- |
| **Startup failure cleanup** | In `server.c`, failure of `pthread_attr_init()`, `pthread_attr_setdetachstate()`, or `sigaction()` can return without releasing the previously created listening socket and/or KV store. These paths are uncommon but real ownership gaps. | Consolidate initialization cleanup and test representative failures where practical. |
| **`pselect()` descriptor limit** | The listener is inserted into an `fd_set` using `FD_SET(server_sock, ...)` without verifying `server_sock < FD_SETSIZE`. A high-numbered descriptor can exceed the range supported by `select`-style sets. | Guard the descriptor range and fail cleanly, or use `poll`/`ppoll` instead. |
| **Unbounded client resource usage** | Every accepted connection gets a detached thread and an input buffer; there is no maximum client count, read deadline, idle timeout, or per-client work quota. | Add client admission limits and connection timeouts before considering broader deployment. |
| **Argument handling** | `--port` is validated when it appears first; unrelated or extra arguments are not consistently rejected. `--replicaof` is explicitly rejected. | Parse all arguments with a single strict option parser, reject unexpected tokens, and add CLI tests. |

These are actionable issues in the current stable source. They are not evidence that the already-tested normal startup, request handling, or shutdown paths are broken.

## 2. Intentional feature and compatibility boundaries

| Area | Current behavior / limitation |
| --- | --- |
| **Commands** | Only `PING`, `ECHO`, `SET`, and `GET` are implemented; `PX` is the supported `SET` option. No `DEL` command is exposed even though the KV module provides an internal removal operation. |
| **Request protocol** | The parser accepts only flat RESP2 arrays containing bulk-string arguments; it is not a complete RESP2/RESP3 parser. |
| **Input limits** | At most 1,024 arguments, 1 MiB per bulk string, and 2 MiB of buffered input per connection. A request exceeding these limits is not supported. |
| **Storage** | Memory-only storage; process exit loses all data. No AOF, RDB persistence, replication, or failover. |
| **Expiration** | `PX` uses monotonic millisecond deadlines and expires entries lazily on lookup. Expired but untouched entries continue to occupy memory until accessed or the store is destroyed. |
| **Concurrency** | A single mutex protects the hash table, serializing all KV operations. The implementation has no benchmarked throughput or scalability guarantees. |
| **Security** | The listener binds to `INADDR_ANY`, with no authentication, TLS, access control, or client quotas. It must not be exposed to untrusted networks. |
| **Platform** | POSIX threads/sockets and Linux-specific `MSG_NOSIGNAL` are used; Linux/WSL2 is the validated environment, not cross-platform portability. |
| **Replication** | The stable build explicitly rejects `--replicaof`. Earlier unfinished experiments are preserved separately in `codecrafters/replication-baseline`. |

## 3. Testing and assurance gaps

The current tests cover parser behavior; fragmented and pipelined reads; binary keys and values; overwrite/expiration; concurrent KV and TCP clients; malformed/truncated input; 1,000 short-lived connections; occupied-port handling; and SIGINT/SIGTERM shutdown with an idle client. The source builds without warnings under `-Werror`, and exercised tests have passed with AddressSanitizer and UndefinedBehaviorSanitizer.

Important qualifications:

- **Race freedom is not proven.** Passing concurrent tests and ASan/UBSan runs is not equivalent to a successful ThreadSanitizer run or a formal concurrency proof.
- **Memory-pressure and fault-injection paths are lightly tested.** Allocation failures, thread-creation failures, unusual descriptor exhaustion, and repeated signal delivery deserve dedicated coverage.
- **No long-duration or hostile-client testing.** Slow senders, idle clients, and a large number of simultaneous clients could exhaust threads, descriptors, or memory. The bounded receive buffer limits a single connection's buffered request bytes but is not a complete resource-control mechanism.
- **No performance claims.** There are no published latency/throughput benchmarks or production workload measurements.

## 4. Resolved issues from the original review

The original 2026-11-13 review described a different codebase. Its major concerns should **not** be repeated as defects of the current stable server:

- The tokenizer was replaced with an incremental, length-aware, binary-safe RESP reader and parser.
- `SET` now replaces existing keys; the KV module maintains entry counts and resizes by relinking nodes.
- TTL math uses 64-bit monotonic timestamps with checks for invalid/overflowing deadlines.
- Shared KV operations are mutex-protected, and `GET` returns a copy that can be sent outside the lock.
- Workers exit on socket closure/error and release their buffers, client descriptors, and bookkeeping objects.
- Partial writes are handled by a send loop rather than assuming one `send()` is sufficient.
- Shutdown uses a registered-client list, `shutdown()` on client sockets, and a condition variable to wait for worker cleanup.
- `pselect()` with controlled signal masks addresses the lost-wakeup window in the original blocking-`accept()` design.
- The original replication and RDB-handling code was removed from the stable build and preserved in a separate branch; its unfinished bugs remain relevant **only when continuing that branch**.

## 5. Recommended order

1. **Small reliability pass:** fix startup-error cleanup and the `FD_SETSIZE` edge case; tighten command-line validation.
2. **Improve repeatability:** run every TCP integration suite against an automatically managed server, including sanitizer variants when feasible.
3. **Commit and present the stable branch:** ensure README, architecture, limitations, and roadmap match the build; merge only after final verification.
4. **Optional future engineering:** client quotas/timeouts, load testing, more commands, and a scalable I/O model if the project grows.
5. **Separate replication track:** resume CodeCrafters work on a dedicated branch with its own tests and compatibility requirements.

**Status:** The core is suitable to describe as a tested educational systems project, with the limitations above disclosed. It is not a claim of production readiness or full Redis compatibility.
