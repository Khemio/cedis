#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>
#include <signal.h>

#include "utils/kv.h"
#include "utils/resp_reader.h"

#define BACKLOG 10
#define REUSE 1

typedef struct threadArgs {
    int clnt_sock;
    KV *kv;

    struct threadArgs *next;
} threadArgs;


static pthread_mutex_t clients_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t clients_empty =
    PTHREAD_COND_INITIALIZER;

static threadArgs *active_clients = NULL;

static volatile sig_atomic_t stop_requested = 0;

int createServer(int port);
int acceptCon(int server_sock);
void *threadMain(void* args);

static void handleStopSignal(int signum) {
    (void)signum;
    stop_requested = 1;
}

static void registerClient(threadArgs *args) {
    pthread_mutex_lock(&clients_mutex);

    args->next = active_clients;
    active_clients = args;

    pthread_mutex_unlock(&clients_mutex);
}

static void releaseClient(threadArgs *args) {
    pthread_mutex_lock(&clients_mutex);

    threadArgs **slot = &active_clients;

    while (*slot != NULL && *slot != args)
        slot = &(*slot)->next;

    if (*slot == NULL)
        abort(); // Internal ownership error.

    *slot = args->next;

    // Main cannot shut down an fd while it is being
    // closed and potentially reused by another client.
    close(args->clnt_sock);

    if (active_clients == NULL)
        pthread_cond_signal(&clients_empty);

    pthread_mutex_unlock(&clients_mutex);

    free(args);
}

static void drainClients(void) {
    pthread_mutex_lock(&clients_mutex);

    // Wake workers blocked in recv().
    for (threadArgs *client = active_clients;
         client != NULL;
         client = client->next) {

        shutdown(client->clnt_sock, SHUT_RDWR);
    }

    while (active_clients != NULL)
        pthread_cond_wait(
            &clients_empty,
            &clients_mutex
        );

    pthread_mutex_unlock(&clients_mutex);
}


static int parsePort(const char *text, int *out) {
    if (text == NULL || *text == '\0' || out == NULL)
        return -1;

    // Require decimal digits only.
    for (const unsigned char *p =
             (const unsigned char *)text; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
    }

    errno = 0;
    char *end = NULL;

    long value = strtol(text, &end, 10);

    if (errno != 0 || *end != '\0' ||
        value < 1 || value > 65535) {
        return -1;
    }

    *out = (int)value;
    return 0;
}

int main(int argc, char** argv) {
	// Disable output buffering
	setbuf(stdout, NULL);

	int server_sock;
	int port = 6379;

	if (argc >= 2 && strcmp(argv[1], "--port") == 0) {
		if (argc < 3 || parsePort(argv[2], &port) < 0) {
			fprintf(stderr,
					"Invalid port: expected an integer "
					"between 1 and 65535\n");
			return EXIT_FAILURE;
		}
	}


	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--replicaof") == 0) {
			fprintf(
				stderr,
				"Replication is not supported in this build.\n"
			);
			return EXIT_FAILURE;
		}
	}


	KV *kv = kv_create();

	if (kv == NULL) {
		fprintf(stderr, "Failed to initialize KV store\n");
		return EXIT_FAILURE;
	}

	server_sock = createServer(port);
	if (server_sock < 0) {
		perror("createServer");
		kv_destroy(kv);
		return EXIT_FAILURE;
	}


	pthread_attr_t worker_attr;

	int err = pthread_attr_init(&worker_attr);

	if (err != 0) {
		fprintf(stderr, "pthread_attr_init: %s\n",
				strerror(err));
		return EXIT_FAILURE;
	}

	err = pthread_attr_setdetachstate(
		&worker_attr,
		PTHREAD_CREATE_DETACHED
	);

	if (err != 0) {
		fprintf(stderr, "pthread_attr_setdetachstate: %s\n",
				strerror(err));
		pthread_attr_destroy(&worker_attr);
		return EXIT_FAILURE;
	}

	struct sigaction action = {0};
	action.sa_handler = handleStopSignal;
	sigemptyset(&action.sa_mask);

	// No SA_RESTART: allow a signal to interrupt accept().
	if (sigaction(SIGINT, &action, NULL) < 0 ||
		sigaction(SIGTERM, &action, NULL) < 0) {
		perror("sigaction");
		return EXIT_FAILURE;
	}

	sigset_t stop_signals;
	sigemptyset(&stop_signals);
	sigaddset(&stop_signals, SIGINT);
	sigaddset(&stop_signals, SIGTERM);

	
	while (!stop_requested) {
		int client_sock = acceptCon(server_sock);

		if (client_sock < 0) {
			if (errno == EINTR)
				continue;

			perror("accept");
			break;
		}

		if (stop_requested) {
			close(client_sock);
			break;
		}

		threadArgs *args = malloc(sizeof *args);

		if (args == NULL) {
			perror("malloc");
			close(client_sock);
			continue;
		}

		*args = (threadArgs){
			.clnt_sock = client_sock,
			.kv = kv
		};

		registerClient(args);

		
		pthread_t thread_id;
		sigset_t previous_mask;

		// Workers inherit blocked shutdown signals.
		int mask_rc = pthread_sigmask(
			SIG_BLOCK, &stop_signals, &previous_mask
		);

		if (mask_rc != 0) {
			fprintf(stderr, "pthread_sigmask: %s\n",
					strerror(mask_rc));
			releaseClient(args);
			break;
		}

		int rc = pthread_create(
			&thread_id,
			&worker_attr,
			threadMain,
			args
		);

		// Restore the main thread's signal mask.
		mask_rc = pthread_sigmask(
			SIG_SETMASK, &previous_mask, NULL
		);

		if (mask_rc != 0)
			abort();

		if (rc != 0) {
			fprintf(stderr, "pthread_create: %s\n",
					strerror(rc));

			releaseClient(args);
			continue;
		}

		// The worker now owns args and client_sock.
	}
	
	printf("Stopping server...\n");

	close(server_sock);

	drainClients();

	pthread_attr_destroy(&worker_attr);

	// All client workers have finished.

	// No worker can access KV after drainClients().
	kv_destroy(kv);

	printf("Server stopped.\n");
	return EXIT_SUCCESS;
}


int createServer(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0)
        return -1;

    int reuse = REUSE;

    if (setsockopt(
            server_sock,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof reuse
        ) < 0) {
        goto fail;
    }

    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(
            server_sock,
            (struct sockaddr *)&addr,
            sizeof addr
        ) < 0) {
        goto fail;
    }

    if (listen(server_sock, BACKLOG) < 0)
        goto fail;

    printf("Waiting for a client to connect...\n");

    return server_sock;

fail:
    {
        int saved_errno = errno;
        close(server_sock);
        errno = saved_errno;
    }

    return -1;
}


int acceptCon(int server_sock) {
    return accept(server_sock, NULL, NULL);
}

static int sendAll(int fd, const void *data, size_t len) {
    const unsigned char *bytes = data;

    while (len > 0) {
        ssize_t sent = send(fd, bytes, len, MSG_NOSIGNAL);

        if (sent > 0) {
            bytes += sent;
            len -= (size_t)sent;
            continue;
        }

        if (sent < 0 && errno == EINTR) {
            continue;
        }

        return -1;
    }

    return 0;
}


static int sendBulk(int fd, const void *data, size_t len) {
    char header[64];

    int n = snprintf(
        header,
        sizeof header,
        "$%zu\r\n",
        len
    );

    if (n < 0 || (size_t)n >= sizeof header) {
        return -1;
    }

    if (sendAll(fd, header, (size_t)n) < 0 ||
        sendAll(fd, data, len) < 0 ||
        sendAll(fd, "\r\n", 2) < 0) {
        return -1;
    }

    return 0;
}


static int commandEquals(const struct resp_arg *arg, const char *name) {
    size_t name_len = strlen(name);

    if (arg->len != name_len) {
        return 0;
    }

    for (size_t i = 0; i < name_len; i++) {
        if (tolower((unsigned char)arg->data[i]) !=
            (unsigned char)name[i]) {
            return 0;
        }
    }

    return 1;
}


static int monotonicMillis(uint64_t *out) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1;
    }

    if (ts.tv_sec < 0) {
        return -1;
    }

    uint64_t seconds = (uint64_t)ts.tv_sec;
    uint64_t milliseconds =
        (uint64_t)ts.tv_nsec / 1000000;

    if (seconds > (UINT64_MAX - milliseconds) / 1000) {
        return -1;
    }

    *out = seconds * 1000 + milliseconds;
    return 0;
}


static int parsePositiveU64(const struct resp_arg *arg, uint64_t *out) {
    if (arg->len == 0) {
        return -1;
    }

    uint64_t value = 0;

    for (size_t i = 0; i < arg->len; i++) {
        unsigned char ch = arg->data[i];

        if (ch < '0' || ch > '9') {
            return -1;
        }

        uint64_t digit = ch - '0';

        if (value > (UINT64_MAX - digit) / 10) {
            return -1;
        }

        value = value * 10 + digit;
    }

    if (value == 0) {
        return -1;
    }

    *out = value;
    return 0;
}


static int handleRespRequest(
    int client_sock,
    const struct resp_request *req,
    KV *kv
){
    const struct resp_arg *command = &req->args[0];

    if (commandEquals(command, "ping")) {
        if (req->argc != 1) {
            const char *err =
                "-ERR wrong number of arguments for PING\r\n";
            return sendAll(client_sock, err, strlen(err));
        }

        return sendAll(client_sock, "+PONG\r\n", 7);
    }

    if (commandEquals(command, "echo")) {
        if (req->argc != 2) {
            const char *err =
                "-ERR wrong number of arguments for ECHO\r\n";
            return sendAll(client_sock, err, strlen(err));
        }

        const struct resp_arg *message = &req->args[1];

		return sendBulk(
			client_sock,
			message->data,
			message->len
		);
    }


	if (commandEquals(command, "get")) {
		if (req->argc != 2) {
			const char *err =
				"-ERR wrong number of arguments for GET\r\n";

			return sendAll(client_sock, err, strlen(err));
		}

		uint64_t now_ms;

		if (monotonicMillis(&now_ms) < 0) {
			return -1;
		}

		const struct resp_arg *key = &req->args[1];

		struct kv_blob value = {0};

		enum kv_status status = kv_get(
			kv,
			key->data,
			key->len,
			now_ms,
			&value
		);

		if (status == KV_NOT_FOUND) {
			// RESP null bulk string.
			return sendAll(client_sock, "$-1\r\n", 5);
		}

		if (status != KV_OK) {
			const char *err =
				"-ERR storage operation failed\r\n";

			return sendAll(client_sock, err, strlen(err));
		}

		int result = sendBulk(
			client_sock,
			value.data,
			value.len
		);

		kv_blob_free(&value);

		return result;
	}


	if (commandEquals(command, "set")) {
		if (req->argc != 3 && req->argc != 5) {
			const char *err =
				"-ERR wrong number of arguments for SET\r\n";

			return sendAll(client_sock, err, strlen(err));
		}

		const struct resp_arg *key = &req->args[1];
		const struct resp_arg *value = &req->args[2];

		enum kv_status status;

		if (req->argc == 5) {
			if (!commandEquals(&req->args[3], "px")) {
				const char *err = "-ERR syntax error\r\n";
				return sendAll(client_sock, err, strlen(err));
			}

			uint64_t ttl_ms;

			if (parsePositiveU64(&req->args[4], &ttl_ms) < 0) {
				const char *err =
					"-ERR invalid expire time in SET\r\n";

				return sendAll(client_sock, err, strlen(err));
			}

			uint64_t now_ms;

			if (monotonicMillis(&now_ms) < 0) {
				return -1;
			}

			status = kv_set_with_ttl(
				kv,
				key->data, key->len,
				value->data, value->len,
				now_ms, ttl_ms
			);

		} else {
			status = kv_set(
				kv,
				key->data, key->len,
				value->data, value->len
			);
		}

		if (status != KV_OK) {
			const char *err =
				status == KV_INVALID
					? "-ERR invalid expire time in SET\r\n"
					: "-ERR storage operation failed\r\n";

			return sendAll(client_sock, err, strlen(err));
		}

		return sendAll(client_sock, "+OK\r\n", 5);
	}



    const char *err =
        "-ERR command not yet implemented\r\n";

    return sendAll(client_sock, err, strlen(err));
}


void *threadMain(void *raw_args) {
    threadArgs *args = raw_args;

    int client_sock = args->clnt_sock;
	KV *kv = args->kv;

    struct resp_reader reader = {
        .fd = client_sock
    };

    for (;;) {
        struct resp_request req = {0};

        enum resp_read_status status =
            resp_reader_next(&reader, &req);

        if (status == RESP_READ_CLOSED) {
            break;
        }

        if (status != RESP_READ_OK) {
            fprintf(stderr,
                "Client read failed: status %d\n",
                (int)status);
            break;
        }

		int result = handleRespRequest(client_sock, &req, kv);

        resp_request_free(&req);

        if (result < 0) {
            perror("send");
            break;
        }
    }

    resp_reader_free(&reader);
	releaseClient(args);

    return NULL;
}
