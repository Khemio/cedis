# Cedis development roadmap

> **Status:** proposed work, not yet implemented. Based on source review of `main` on 2026-11-13. Priorities are suggestions; progress should be updated as the code changes.

**Goal:** Finish the CodeCrafters Redis replication milestone on top of a correct, testable core, then stop and present Cedis as a completed educational systems project. Do not aim for full Redis compatibility.

## Milestone 1 — Make the server safe enough to extend (P0)

- [ ] **C-01: Repair immediate memory-safety errors.** Fix `initMaster`/`initSlave` allocation sizes, `hexToBinary` pointer arithmetic, unsafe parser token ownership, and unbounded/undersized response formatting. Add clean ownership and destructors for request objects and metadata. **Done when:** AddressSanitizer and UndefinedBehaviorSanitizer pass basic command paths.
- [ ] **C-02: Fix network connection lifecycles.** Break on `recv() == 0`; handle `recv() < 0`, send failures, cleanup, thread detach/join policy, and accepted socket ownership. **Done when:** Disconnecting clients does not hang workers, leak resources, or spin a CPU.
- [ ] **C-03: Replace the tokenizer with an incremental RESP parser.** Keep a per-connection input buffer, parse length-prefixed arrays/bulk strings, support partial and coalesced reads, enforce bounds, and preserve key/value payload bytes. Only compare command names case-insensitively. **Done when:** fragmented, pipelined, empty, mixed-case, and malformed requests have deterministic tests.

## Milestone 2 — Correctness and repeatable tests (P1)

- [ ] **C-04: Fix dictionary invariants.** Make `SET` replace existing keys, decrement entry count on delete, and keep resize behavior consistent. **Done when:** overwrite/delete/reinsert and resize tests pass with correct `DictSize`.
- [ ] **C-05: Correct TTL semantics.** Use consistent 64-bit millisecond types, avoid integer truncation, and define overwrite/expiry behavior. Test expiration boundaries and expired-key deletion. **Done when:** `SET key value PX n` and `GET` behave predictably over representative durations.
- [ ] **C-06: Define shared-state concurrency.** Either guard the dictionary and replication bookkeeping with locks or adopt a single-threaded event loop. Keep socket I/O outside long-held locks. **Done when:** concurrent SET/GET tests pass ThreadSanitizer (where supported) or equivalent race testing.
- [ ] **C-07: Add automated verification.** Unit-test dictionary and parser; run multi-client integration tests through TCP; add sanitizer-friendly build flags and a documented test command. **Done when:** a clean build runs automated tests without manual `nc` requests.

## Milestone 3 — Complete replication (P1/P2)

- [ ] **C-08: Fix replica startup and role lifecycle.** Initialize listening socket before use, parse `--replicaof` safely, keep upstream master connection distinct from client sockets, and manage threads/resources explicitly. **Done when:** master and replica launch reliably on different ports.
- [ ] **C-09: Finish the replica handshake.** Implement `PING`, `REPLCONF listening-port`, `REPLCONF capa psync2`, and `PSYNC ? -1` with response validation over a buffered stream. **Done when:** the handshake works even when responses are fragmented or combined.
- [ ] **C-10: Handle full resynchronization framing.** Parse `+FULLRESYNC`, replication ID and offset, then consume the binary RDB bulk payload safely without treating it as RESP commands. State explicitly whether initial snapshots contain real data or only the CodeCrafters empty-RDB fixture. **Done when:** a replica stays connected and can read subsequent commands after the snapshot.
- [ ] **C-11: Propagate write commands correctly.** Maintain replica connections and master write-ordering; forward complete RESP command frames (including options and exact value bytes), and apply upstream writes without responding as a normal client. **Done when:** writes issued to master appear on one and multiple replicas.
- [ ] **C-12: Track offsets, ACK, and WAIT.** Count propagated bytes correctly; respond to `REPLCONF GETACK *`, process replica `ACK` messages, and implement `WAIT replicas timeout`. **Done when:** timeout, zero-replica, partial-ack and multi-replica cases pass the relevant CodeCrafters stages.
- [ ] **C-13: Add replication integration tests.** Test startup, handshake, write propagation, multiple replicas, `WAIT`, disconnects, and restart behavior. **Done when:** replication tests run repeatedly without hangs or races.

## Milestone 4 — Finish and document (P2)

- [ ] **C-14: Modularize `server.c` where it simplifies ownership.** Separate connection handling, commands, protocol parsing/serialization, storage and replication. Do not refactor blindly before behavior is verified.
- [ ] **C-15: Final portfolio pass.** Update README's implemented-features table, list supported CodeCrafters stages, record known limitations, provide one reproducible demo and clean build/test instructions. **Done when:** documented claims match automated behavior.

## Suggested execution order

`C-01 → C-02 → C-03 → C-04 → C-05 → C-06 → C-07 → C-08 → C-09 → C-10 → C-11 → C-12 → C-13 → C-14 → C-15`.

Some tasks can overlap, but **avoid finishing replication on top of the current parser and unsafe shared state**. A narrow first success is master + one replica + one propagated SET; then extend it to multi-replica ACK/WAIT behavior.

## Deliberately deferred

Persistent RDB/AOF storage, Redis Streams, transactions, pub/sub, sorted sets, authentication, clustering, failover, and broad Redis compatibility. They are potential future experiments, **not required** to close this project.

**Definition of done:** Core command paths pass repeatable tests; replication passes the intended CodeCrafters stages; sanitizer runs find no outstanding problems in exercised paths; and documentation does not claim more than the code supports.
