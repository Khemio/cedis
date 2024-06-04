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

#include "utils/parser.h"
#include "utils/timing.h"
#include "utils/dict.h"

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

typedef struct {
	int clnt_sock;
	Dict store;
	Info info;
} threadArgs;

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
	// TODO: Extract and add mem cleanup
	Info info;
	info = malloc(sizeof(Info));
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
	// TODO: Extract and add mem cleanup
	Info info;
	info = malloc(sizeof(Info));
	info->size = 1;
	// info->size = 3;

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

int connectToSlave(int *slave_sock, const char *address, const int port) {
	int reuse = REUSE;

	*slave_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (*slave_sock == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	//
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	if (setsockopt(*slave_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in slave_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(port),
									 .sin_addr = { htonl(inet_network(address)) },
									};

	if (connect(*slave_sock, (struct sockaddr *)&slave_addr, sizeof(slave_addr)) < 0) {
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

	recv(master_sock, recv_buff, sizeof(recv_buff), 0);
	printf("%s\n", recv_buff);

	return 0;
}

void *listenToMaster(void* args) {

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

	if (argc >= 5 && strcmp(argv[3], "--replicaof") == 0) {
		info = initSlave();
		char *address = strtok(argv[4], " ");
		
		if (strcmp(argv[4], "localhost") == 0) {
			masterAddr = "127.0.0.1";
		} else {
			masterAddr = strdup(address);
		}
		masterPort = atoi(strtok(NULL, "\0"));
		// !!!
		connectToMaster(&master_sock, masterAddr, masterPort);
		handshake(master_sock, port, info);

		pthread_t threadId;
		threadArgs *args; 

		client_sock = acceptCon(server_sock);

		printf("Client connected\n");

		if ((args = malloc(sizeof(threadArgs))) == NULL) printf("malloc() fail\n");
		
		args->clnt_sock = master_sock;
		args->store = store;
		args->info = info;

		if (pthread_create(&threadId, NULL, threadMain, args) != 0) printf("phtread_create() fail\n");

	} else {
		info = initMaster();
	}

	

	// !!!
	server_sock = createServer(port);

	for (;;) {
		pthread_t threadId;
		threadArgs *args; 

		client_sock = acceptCon(server_sock);

		printf("Client connected\n");

		if ((args = malloc(sizeof(threadArgs))) == NULL) printf("malloc() fail\n");
		
		args->clnt_sock = client_sock;
		args->store = store;
		args->info = info;

		if (pthread_create(&threadId, NULL, threadMain, args) != 0) printf("phtread_create() fail\n");
	}
	
	close(server_sock);
	DictDestroy(store);

	return 0;
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
	int client_sock;
	socklen_t client_addr_len;
	struct sockaddr_in client_addr;

	client_addr_len = sizeof(client_addr);

	// Add error handling
	client_sock = accept(server_sock, (struct sockaddr *) &client_addr, &client_addr_len);
	if (client_sock < 0) printf("accept() fail\n");

	return client_sock;
}

void *threadMain(void* args) {
	int client_sock = ((threadArgs *)args) -> clnt_sock;
	Dict store = ((threadArgs *)args) -> store;
	Info info = ((threadArgs *)args) -> info;

	char buff[256];
	int recvSize = 1;

	struct request *req;
    req = malloc(sizeof(struct request));

	for (;;) {
		recvSize = recv(client_sock, buff, sizeof(buff), 0);
		// printf("%d\n%s\n", recvSize, info->entries[0].value.str);
		if (strcmp(info->entries[0].value.str, "slave") == 0) {
			printf("%s\n", buff);
		}
	
		// if (buff[0] != '*') printf("size: %d, start: %c\n", recvSize, buff[0]);


		if (recvSize > 0) {
			// RDB Transfer fucks up parser
			parseReq(req, buff, recvSize);
			handleReq(client_sock, req, store, info);
	

			memset(buff, 0, sizeof(buff));
		}
	}

	close(client_sock);

	return 0;
}

void handleReq(int client_sock, const struct request *req, Dict store, Info info) {
	printf("%s\n", req->params[0].val);
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

		// if (strcmp(info->entries[0].value.str, "slave")) {}

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
			// Save port
			int port = atoi(req->params[2].val);
			// int slave_sock;
			// connectToSlave(&slave_sock, "127.0.0.1", port);

			// append(info->entries[3].value.lt, slave_sock);
			append(info->entries[3].value.lt, client_sock);
			// printf("%d\n", client_sock);

			send(client_sock, res, strlen(res), 0);
		} else if (strncmp(req->params[1].val, "capa", req->params[1].len) == 0) {
			send(client_sock, res, strlen(res), 0);
		}				
	} else if (strncmp(req->params[0].val, "psync", req->params[0].len) == 0) {
		char send_buff[512];
		size_t buff_size = 0;
		char *id;
		char offset;

		// printf("%d\n", client_sock);

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
			
			printf("%d\n", client_sock);
			int sendSize = send(client_sock, send_buff, buff_size, 0);	
			printf("Sent: %d\n", sendSize);
			
		}	
	}
}