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
#include "utils/parser.h"
#include "utils/timing.h"
#include "utils/dict.h"
#include "utils/resp_reader.h"

#define BACKLOG 10
#define REUSE 1
#define EMPTY_RDB_HEX 													\
"524544495330303131fa0972656469732d76657205372e322e30fa0a72656469732d6"	\
"2697473c040fa056374696d65c26d08bc65fa08757365642d6d656dc2b0c41000fa08"	\
"616f662d62617365c000fff06e3bfec0ff5aa2"							


enum TypeCode {TYPE_INT, TYPE_STRING, TYPE_LIST};

typedef struct node {
	int fd;
	struct node *next;
} *node;

typedef struct list {
	int len;
	node head;
} *list;

typedef struct {
	char *key;
	enum TypeCode typeCode;
	union {
		char *str;
		int num;
		list lt;
	} value;
} entrie;

typedef struct Info {
	int size;
	entrie *entries;
} *Info;

typedef struct threadArgs {
    int clnt_sock;
    Dict store;
    Info info;
    KV *kv;

    struct threadArgs *next;
} threadArgs;


static pthread_mutex_t clients_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t clients_empty =
    PTHREAD_COND_INITIALIZER;

static threadArgs *active_clients = NULL;

static volatile sig_atomic_t stop_requested = 0;

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


int createServer(int port);
int acceptCon(int server_sock);
void handleReq(int cln_sock, const struct request *req, Dict store, Info info);

char *hexToBinary(char *hex, size_t binary_file_len) {
	char *binary_file = malloc(binary_file_len) + 1;
	char *pos = hex;
	binary_file[binary_file_len] = '\0';
	int index;

	for (index = 0; index < binary_file_len; index++) {
		char hex1 = hex[index * 2];
		int b1;
		if (hex1 >= '0' && hex1 <= '9') {
			b1 = hex1 - '0';
		} else {
			b1 = hex1 - 'a' + 10;
		}

		char hex2 = hex[index * 2 + 1];
		int b2;
		if (hex2 >= '0' && hex2 <= '9') {
			b2 = hex2 - '0';
		} else {
			b2 = hex2 - 'a' + 10;
		}

		if (index > 62) {
			printf("%c%c", b1, b2);
		}

		binary_file[index] = (b1 << 4) | b2;
	}

	return binary_file;
}

int append(list l, int sock_fd) {
	// TODO: Add error handling
	node n = malloc(sizeof(struct node));
	node temp;

	n->fd = sock_fd;
	n->next = NULL;

	if (l->head == NULL) {
		l->head = n;
		l->len++;
		return 0;
	}

	for (temp = l->head; temp->next != NULL; temp = temp->next) {
		if (temp->fd == sock_fd) {
			return 1;
		}
	}

	temp->next = n;
	l->len++;
	return 0;
}

Info initMaster(void) {
	Info info = malloc(sizeof *info);

    if (info == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    info->size = 4;

	info->entries = malloc(info->size * sizeof(entrie));

	info->entries[0].key = strdup("role");
	info->entries[0].typeCode = TYPE_STRING;
	info->entries[0].value.str = strdup("master");

	info->entries[1].key = strdup("master_replid");
	info->entries[1].typeCode = TYPE_STRING;
	info->entries[1].value.str = strdup("8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb");

	info->entries[2].key = strdup("master_repl_offset");
	info->entries[2].typeCode = TYPE_INT;
	info->entries[2].value.num = 0;

	info->entries[3].key = strdup("repl_socks");
	info->entries[3].typeCode = TYPE_LIST;
	info->entries[3].value.lt = malloc(sizeof(struct list));
	info->entries[3].value.lt->len = 0;
	info->entries[3].value.lt->head = NULL;

	return info;
}

Info initSlave(void) {
	Info info = malloc(sizeof *info);

    if (info == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    info->size = 4;

	info->entries = malloc(info->size * sizeof(entrie));

	info->entries[0].key = strdup("role");
	info->entries[0].typeCode = TYPE_STRING;
	info->entries[0].value.str = strdup("slave");

	// info->entries[1].key = strdup("slve_replid");
	// info->entries[1].typeCode = TYPE_STRING;
	// info->entries[1].value.str = strdup("?");

	// info->entries[2].key = strdup("slave_repl_offset");
	// info->entries[2].typeCode = TYPE_INT;
	// info->entries[2].value.num = 0;

	return info;
}


static void freeInfo(Info info) {
    if (info == NULL)
        return;

    if (info->entries != NULL) {
        for (int i = 0; i < info->size; i++) {
            entrie *entry = &info->entries[i];

            free(entry->key);

            switch (entry->typeCode) {
                case TYPE_STRING:
                    free(entry->value.str);
                    break;

                case TYPE_LIST: {
                    list l = entry->value.lt;

                    if (l == NULL)
                        break;

                    node current = l->head;

                    while (current != NULL) {
                        node next = current->next;
                        free(current);
                        current = next;
                    }

                    free(l);
                    break;
                }

                case TYPE_INT:
                    break;
            }
        }

        free(info->entries);
    }

    free(info);
}


int connectToMaster(int *master_sock, const char *address, const int port) {
	int reuse = REUSE;

	*master_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (*master_sock == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	//
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	if (setsockopt(*master_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in master_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(port),
									 .sin_addr = { htonl(inet_network(address)) },
									};

	if (connect(*master_sock, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
		printf("connect() failed: %s \n", strerror(errno));
		return 1;
	}
	return 0;
}

int handshake(int master_sock, int port, Info info) {
	char *send_buff;
	char recv_buff[128];
	char *ping = "*1\r\n$4\r\nPING\r\n";
	// char confPort[50] = "*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n6380\r\n";
	char confPort[50] = "*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n6380\r\n";
	char *confCapa = "*3\r\n$8\r\nREPLCONF\r\n$4\r\ncapa\r\n$6\r\npsync2\r\n";
	char *psync = "*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$2\r\n-1\r\n";

	send_buff = strdup(ping);
	send(master_sock, send_buff, strlen(send_buff), 0);

	// check for +PONG
	recv(master_sock, recv_buff, sizeof(recv_buff), 0);
	printf("%s\n", recv_buff);

	bzero(recv_buff, sizeof(recv_buff));

	// REPLCONF 1
	sprintf(confPort, "*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n%d\r\n", port);
	send_buff = strdup(confPort);
	send(master_sock, send_buff, strlen(send_buff), 0);

	// check for +OK
	recv(master_sock, recv_buff, sizeof(recv_buff), 0);
	printf("%s\n", recv_buff);

	bzero(recv_buff, sizeof(recv_buff));

	// REPLCONF 2
	send_buff = strdup(confCapa);
	send(master_sock, send_buff, strlen(send_buff), 0);

	// check for +OK
	recv(master_sock, recv_buff, sizeof(recv_buff), 0);
	printf("%s\n", recv_buff);

	bzero(recv_buff, sizeof(recv_buff));

	// PSYNC
	send_buff = strdup(psync);
	send(master_sock, send_buff, strlen(send_buff), 0);

	// check for +FULLRESYNC <REPL_ID> 0\r\n
	recv(master_sock, recv_buff, sizeof(recv_buff), 0);
	printf("%s\n", recv_buff);

	bzero(recv_buff, sizeof(recv_buff));

	// TODO: Handle RDB file transfer
	recv(master_sock, recv_buff, sizeof(recv_buff), 0);
	printf("%s\n", recv_buff);

	return 0;
}

void *threadMain(void* args);

int main(int argc, char** argv) {
	// Disable output buffering
	setbuf(stdout, NULL);

	int server_sock, client_sock, master_sock;
	int port;
	Info info;
	char *masterAddr;
	int masterPort;

	if (argc >= 3 && strcmp(argv[1], "--port") == 0) {
		port = atoi(argv[2]);
	} else {
		port = 6379;
	}

	Dict store;
	store = DictCreate();

	KV *kv = kv_create();

	if (kv == NULL) {
		fprintf(stderr, "Failed to initialize KV store\n");
		return EXIT_FAILURE;
	}

	if (argc >= 5 && strcmp(argv[3], "--replicaof") == 0) {
		info = initSlave();
		char *address = strtok(argv[4], " ");
		
		if (strcmp(argv[4], "localhost") == 0) {
			masterAddr = "127.0.0.1";
		} else {
			masterAddr = strdup(address);
		}
		masterPort = atoi(strtok(NULL, "\0"));
		connectToMaster(&master_sock, masterAddr, masterPort);
		handshake(master_sock, port, info);

		// TODO: Extract and encapsulate (1)
		pthread_t threadId;
		threadArgs *args; 

		client_sock = acceptCon(server_sock);

		printf("Client connected\n");

		if ((args = malloc(sizeof(threadArgs))) == NULL) printf("malloc() fail\n");
		
		args->clnt_sock = master_sock;
		args->store = store;
		args->info = info;
		args->kv = kv;

		if (pthread_create(&threadId, NULL, threadMain, args) != 0) printf("phtread_create() fail\n");

	} else {
		info = initMaster();
	}

	server_sock = createServer(port);


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
			.store = store,
			.info = info,
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
	freeInfo(info);

	// No worker can access KV after drainClients().
	kv_destroy(kv);
	DictDestroy(store);

	printf("Server stopped.\n");
	return EXIT_SUCCESS;
}

int createServer(int port) {
	int server_sock;
	int reuse = REUSE;

	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	//
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(port),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	if (bind(server_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		// return 1;
		exit(1);
	}
	

	if (listen(server_sock, BACKLOG) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	printf("Waiting for a client to connect...\n");

	return server_sock;
}

int acceptCon(int server_sock) {
    return accept(server_sock, NULL, NULL);
}

// int acceptCon(int server_sock) {
// 	int client_sock;
// 	socklen_t client_addr_len;
// 	struct sockaddr_in client_addr;

// 	client_addr_len = sizeof(client_addr);

// 	// Add error handling
// 	client_sock = accept(server_sock, (struct sockaddr *) &client_addr, &client_addr_len);
// 	if (client_sock < 0) printf("accept() fail\n");

// 	return client_sock;
// }

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

void handleReq(int client_sock, const struct request *req, Dict store, Info info) {
	if (strncmp(req->params[0].val, "ping", req->params[0].len) == 0) {
			char *res = "+PONG\r\n";
			send(client_sock, res, strlen(res), 0);

	} else if (strncmp(req->params[0].val, "echo", req->params[0].len) == 0) {
		char msg[256];
		size_t msgSize;

		msgSize = sprintf(msg,"$%d\r\n%s\r\n", req->params[1].len, req->params[1].val);

		send(client_sock, msg, msgSize, 0);
	} else if (strncmp(req->params[0].val, "set", req->params[0].len) == 0) {
		char send_buf[128];
		int expiry = 0;
		int now;

		char *cmd = strdup(req->params[0].val);
		char *key = strdup(req->params[1].val);
		char *val = strdup(req->params[2].val);


		if (req->nParam > 3) {
			if (strncmp(req->params[3].val, "px", req->params[3].len) == 0) {
				expiry = atoi(req->params[4].val);
			}
		}

		now = time_mili();
		// TODO: add mutex?
		DictInsert(store, key, val, expiry, now + expiry);

		if (strcmp(info->entries[0].value.str, "master") == 0) {
			char *res = "+OK\r\n";
			send(client_sock, res, strlen(res), 0);

			int send_len = sprintf(send_buf, "*3\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n$%lu\r\n%s\r\n", strlen(cmd), cmd, strlen(key), key, strlen(val), val);
		
			// Send to all
			node temp = info->entries[3].value.lt->head;

			for (; temp != NULL; temp = temp->next) {
				printf("FD: %d\n", temp->fd);
				int sendSize = send(temp->fd, send_buf, send_len, 0);
				printf("Sent: %d\n", sendSize);
			}
		}

		free(cmd);
		free(key);
		free(val);
	} else if (strncmp(req->params[0].val, "get", req->params[0].len) == 0) {
		char msg[256];
		size_t msgSize;
		response *res;
		res = malloc(sizeof(response));
		int found = 0;
		found = DictSearch(store, res, req->params[1].val);

		if (found > 0) {
			char *value;
			long expire_at;
			int expiry;
			

			int now = time_mili();

			value = strdup(res->value);
			expire_at = res->expire_at;
			expiry = res->expiry;

			// printf("expiry: %d, now: %d, expire_at: %lu\n", expiry, now, expire_at);

			if (expiry == 0 || (expire_at - now) > 0) {
				msgSize = sprintf(msg,"$%lu\r\n%s\r\n", strlen(value), value);
			} else {
				DictDelete(store, req ->params[1].val);
				msgSize = sprintf(msg,"$-1\r\n");
			}
			
		} else {
			printf("not found\n");
			msgSize = sprintf(msg,"$-1\r\n");
		}
		
		send(client_sock, msg, msgSize, 0);		
	} else if (strncmp(req->params[0].val, "info", req->params[0].len) == 0) {
		char send_buff[512];
		size_t buff_size = 0;

		if (req->nParam > 1) {
			if (strncmp(req->params[1].val, "replication", req->params[1].len) == 0) {
				char *msg;
				size_t msgSize = 0;
				msg = malloc(sizeof(char));

				for (int i = 0; i < info->size; i++) {
					char *payload;
					size_t payloadSize = 0;

					if (info->entries[i].typeCode == TYPE_LIST) continue;

					if (info->entries[i].typeCode == TYPE_STRING) {
						payload = malloc(strlen(info->entries[i].key) + strlen(info->entries[i].value.str) + 2);
						payloadSize = sprintf(payload, "%s:%s", info->entries[i].key, info->entries[i].value.str);
					} else {
						payload = malloc(strlen(info->entries[i].key) + sizeof(int) + 2);
						payloadSize = sprintf(payload, "%s:%d", info->entries[i].key, info->entries[i].value.num);
					}

					msg = realloc(msg, msgSize + payloadSize);

					msgSize += sprintf(&msg[msgSize],"%s\n", payload);

					free(payload);
				}

				buff_size += sprintf(send_buff,"$%lu\r\n%s\r\n", msgSize, msg);
				free(msg);
			}
		}

		send(client_sock, send_buff, buff_size, 0);		
	} else if (strncmp(req->params[0].val, "replconf", req->params[0].len) == 0) {
		char *res = "+OK\r\n";

		if (strncmp(req->params[1].val, "listening-port", req->params[1].len) == 0) {
			int port = atoi(req->params[2].val);
			append(info->entries[3].value.lt, client_sock);

			send(client_sock, res, strlen(res), 0);
		} else if (strncmp(req->params[1].val, "capa", req->params[1].len) == 0) {
			send(client_sock, res, strlen(res), 0);
		}				
	} else if (strncmp(req->params[0].val, "psync", req->params[0].len) == 0) {
		char send_buff[512];
		size_t buff_size = 0;
		char *id;
		char offset;

		if (strncmp(req->params[1].val, "?", req->params[1].len) == 0 && atoi(req->params[2].val) == -1) {
			for (int i = 0; i < info->size; i++) {
				if (strcmp(info->entries[i].key, "master_replid") == 0 || 
					strcmp(info->entries[i].key, "master_repl_offset") == 0) {
						
					if (info->entries[i].typeCode == TYPE_STRING) {
						id = info->entries[i].value.str;
					} else {
						offset =info->entries[i].value.num;
					}	
				}
			}

			buff_size += sprintf(send_buff,"+FULLRESYNC %s %d\r\n", id, offset);

			send(client_sock, send_buff, buff_size, 0);	

			memset(send_buff, 0, buff_size);
			buff_size = 0;

			char *hex_file = EMPTY_RDB_HEX;
			size_t binary_file_len = strlen(hex_file) / 2;
			char *binary_file = hexToBinary(hex_file, binary_file_len);

			buff_size += sprintf(send_buff,"$%ld\r\n", binary_file_len);
			memcpy(send_buff + buff_size, binary_file, binary_file_len);
			buff_size += binary_file_len;
			// send_buff[buff_size] = '\n';
			
			int sendSize = send(client_sock, send_buff, buff_size, 0);	
		}	
	}
}