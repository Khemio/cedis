
CC := gcc

CFLAGS := -std=gnu11 -Wall -Wextra -Wpedantic -g -O0
TEST_CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g -O0

SAN ?=

LEGACY_SRC := utils/dict.c utils/parser.c utils/timing.c
RESP_SRC := utils/resp.c utils/resp_stream.c utils/resp_reader.c
KV_SRC := utils/kv.c

SERVER_SRC := server.c $(LEGACY_SRC) $(RESP_SRC) $(KV_SRC)

.PHONY: all server test clean

all: cedis-dev

cedis-dev: $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SAN) -o $@ $^ -pthread

# Preserve the previous build target for compatibility.
server: server.exe

server.exe: $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SAN) -o $@ $^ -pthread

test-resp: tests/test_resp.c utils/resp.c utils/resp_stream.c
	$(CC) $(TEST_CFLAGS) $(SAN) -o $@ $^ -pthread

test-resp-reader: tests/test_resp_reader.c $(RESP_SRC)
	$(CC) $(TEST_CFLAGS) $(SAN) -o $@ $^ -pthread

test-kv: tests/test_kv.c utils/kv.c
	$(CC) $(TEST_CFLAGS) $(SAN) -o $@ $^ -pthread

test-kv-threads: tests/test_kv_threads.c utils/kv.c
	$(CC) $(TEST_CFLAGS) $(SAN) -o $@ $^ -pthread

test-dict: tests/test_dict.c utils/dict.c
	$(CC) $(TEST_CFLAGS) $(SAN) -o $@ $^ -pthread

test: test-resp test-resp-reader test-kv test-kv-threads test-dict
	./test-resp
	./test-resp-reader
	./test-kv
	./test-kv-threads
	./test-dict

clean:
	rm -f cedis-dev server.exe test-resp test-resp-reader \
	      test-kv test-kv-threads test-dict
