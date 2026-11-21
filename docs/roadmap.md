# Cedis development roadmap

> **Status:** updated 2026-11-20 from the stable `cleanup/cv-ready` implementation and its test results. The earlier roadmap described the original 2026-11-13 codebase. CodeCrafters replication experiments are preserved separately in `codecrafters/replication-baseline`.

## Goal and scope

**Immediate goal:** finish a small, verifiable, portfolio-ready Redis-inspired server and merge it into `main` after a final review. The stable server does **not** need replication, full Redis compatibility, or additional commands to meet this goal.

**Separate optional goal:** continue the CodeCrafters replication stages on a dedicated development branch, using the stable networking and storage work where practical and preserving the challenge's launch/build contract.

Cedis is an educational systems project, not a hardened service for untrusted networks.

## 1. Stable core: progress against the original tasks

| ID | Status | Current result / remaining scope |
| --- | --- | --- |
| **C-01 — Memory safety and ownership** | **Complete for stable core** | The unsafe legacy tokenizer, RDB conversion, and replication command paths were removed from the stable branch; parsed requests and storage values have explicit ownership. Exercised ASan/UBSan tests pass. This does **not** mean the original replication branch was repaired or that every possible bug has been excluded. |
| **C-02 — Connection lifecycle** | **Complete** | Worker threads handle EOF, malformed or truncated requests, read/send errors, and cleanup. Detached workers unregister and close their sockets; shutdown waits for all registered clients. |
| **C-03 — Incremental RESP parser** | **Complete within supported subset** | Binary-safe flat arrays of RESP2 bulk strings; fragmentation, pipelining, bounded input, and explicit parse errors. Nested RESP and RESP3 remain out of scope. |
| **C-04 — Dictionary invariants** | **Complete for new `KV`** | Existing keys are replaced, entry counts are maintained, and growth relinks chained entries. The retired `Dict` implementation is not part of the stable build. |
| **C-05 — TTL correctness** | **Complete within supported subset** | `SET ... PX` uses checked positive 64-bit millisecond durations and `CLOCK_MONOTONIC`. `GET` lazily removes expired keys. Background expiration is not implemented. |
| **C-06 — Shared-state concurrency** | **Implementation complete; further assurance optional** | A mutex serializes KV operations; `kv_get()` returns an owned copy for unlocked network writes. Concurrent unit and TCP tests pass. ThreadSanitizer or a deeper race audit would increase confidence. |
| **C-07 — Automated verification** | **Mostly complete** | Four unit-test executables and process-lifecycle tests run with `make test`; separate Python suites cover TCP smoke, integration, error handling, and 1,000 connections. Full TCP suites are not yet integrated into one self-contained test target. |
| **C-14 — Modularization** | **Partially complete** | RESP parsing, buffering, socket reading, and storage are separate modules. Startup, networking, command dispatch, and worker management remain together in `server.c`; further splitting is optional, not a prerequisite to presenting the project. |
| **C-15 — Portfolio pass** | **In progress** | README, architecture, and limitations were updated to reflect the stable core. Remaining tasks: update this roadmap, complete final checks, review the Git diff, and merge into `main`. |

## 2. Short finishing pass — before merging to `main`

These are small, bounded tasks. Keep them separate from feature work and re-run tests after each code change.

### P0 — Reliability fixes

- [x] **F-01: Clean up early startup failures.** Ensure failures of `pthread_attr_init()`, `pthread_attr_setdetachstate()`, and `sigaction()` release the listening socket, KV store, and any successfully initialized pthread attributes. Prefer a consistent cleanup path without changing normal shutdown ordering. Verify startup still succeeds and occupied-port handling still exits nonzero.
- [x] **F-02: Guard the `pselect()` descriptor range.** Verify the listening fd is representable by `fd_set` before `FD_SET`, or migrate the listener to `poll`/`ppoll`. Do not call `FD_SET` with an out-of-range descriptor.
- [x] **F-03: Parse CLI arguments strictly.** Accept the documented no-argument mode and `--port <1..65535>`; reject missing, duplicate, unknown, or extra tokens. Keep a clear error for `--replicaof` on the stable branch. Test representative invalid invocations.

### P1 — Final verification and presentation

- [x] **F-04: Automate the full TCP integration suite.** Start a dedicated server subprocess on a controlled port, run smoke/integration/connection/error suites, and always stop and reap it. Alternatively, make the existing scripts accept an injected port. Keep failures and logs visible; avoid a dependency on a manually running server.
- [x] **F-05: Final clean-build and sanitizer pass.** On a clean checkout, verify a `-Werror` build, `make test`, all TCP suites, and lifecycle tests with ASan/UBSan. Record the exact commands and any environment-specific sanitizer issues instead of treating a single passing run as a proof of safety.
- [ ] **F-06: Review and merge.** Verify `.gitignore`, `README.md`, architecture, limitations, roadmap, and the branch diff. Confirm `codecrafters/replication-baseline` remains available. Merge `cleanup/cv-ready` into `main` only after the final checks; do not merge the unfinished replication experiment.

**Portfolio-ready definition:** supported commands and build instructions match the code; the complete stable test suite is repeatable; known limitations are explicit; the stable branch is committed and clean. This does not require more Redis features.

## 3. Optional improvements after the portfolio milestone

These are not blockers for CV use and should be prioritized by learning value rather than scope growth.

- **Resource limits:** cap concurrent clients/threads; consider idle/read deadlines and total memory budgets. The current thread-per-client design has no admission limit.
- **Protocol resilience:** add slow-client, oversized-input, allocation-failure, and repeated-signal tests. Consider structured error reporting rather than numeric reader statuses in normal logs.
- **Concurrency assurance:** explore ThreadSanitizer where supported, review shutdown/error paths, and benchmark lock contention before changing the synchronization model.
- **Architecture:** separate command dispatch and reply serialization from `server.c` if they grow enough to justify additional files.
- **Optional small commands:** `DEL` is a natural candidate because `kv_remove()` already exists, but no additional command is required for the current milestone.
- **Portability:** current code targets Linux/WSL2 and uses POSIX APIs plus Linux-specific socket behavior; cross-platform support is deferred.

## 4. Dedicated CodeCrafters replication track — deferred

The original replication work is preserved on `codecrafters/replication-baseline`. Resume it on a separate working branch when ready. The baseline is a snapshot, **not** evidence of working replication.

| Original ID | Work remaining | Acceptance criterion |
| --- | --- | --- |
| **C-08** | Replica startup and role lifecycle | Master and replica start on distinct ports with managed upstream connections and a compatible CodeCrafters launcher. |
| **C-09** | Replication handshake | Validate `PING`, `REPLCONF`, and `PSYNC` exchanges across fragmented/coalesced network reads. |
| **C-10** | Full resynchronization framing | Parse `FULLRESYNC` metadata and binary RDB framing without confusing snapshot bytes with subsequent commands. |
| **C-11** | Write propagation | Preserve exact command bytes/options and apply writes in order on one or more replicas. |
| **C-12** | Offsets, acknowledgments, `WAIT` | Verify offset accounting, `GETACK`/`ACK`, timeout, and replica-count behavior. |
| **C-13** | Replication integration tests | Cover reconnects, multiple replicas, propagation, acknowledgments, and failure paths. |

A reasonable first replication checkpoint is **one replica successfully handshaking and receiving a subsequent `SET`**, with repeatable tests. Expand to multi-replica propagation and `WAIT` afterward. Confirm the CodeCrafters executable/entrypoint requirements before replacing or rearranging build files.

## 5. Deliberately out of scope

Persistence (RDB/AOF), authentication, TLS, transactions, pub/sub, streams, lists, sorted sets, clustering, failover, production observability, and general Redis compatibility. They may be useful future experiments, but should not delay the stable-core milestone.

For current implementation details, see [architecture](architecture.md). For risks and compatibility boundaries, see [limitations](limitations.md).
