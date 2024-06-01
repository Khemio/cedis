#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"

void parseReq(struct request* req, const char* recv, size_t len) {
	char *buff;
	char **items;
	char *token;
	int idx = 0;
	int parseLen;

	// printf("%s\n", recv);


	buff = calloc(len, sizeof(char));
	token = calloc(len, sizeof(char));

	strncpy(buff, recv, len);

	token = strtok(buff, "\r");

	// TODO: Make sure this works for different inputs
	if (token[0] == '*') req->nParam = (token[1] - '0');
	if (token[0] != '*') {
		printf("Unexpected token\n");
		// char *cmd = "rdb";
		// req->nParam = 1;
		// req->params[0].len = strlen(cmd);
		// req->params[0].val = strdup(cmd);
		// return;
	}

	// req->params = calloc((req->nParam - 1) / 2 , sizeof(struct param));
	req->params = calloc(req->nParam , sizeof(struct param));
	memset(token, 0, strlen(token));

	while ((token = strtok(NULL, "\r")) != NULL) {
        token++;

        if (token[0] == '$') {
            // req->params[idx].len = (int)(token[1] - '0');
			token++;
            req->params[idx].len = atoi(token);

        } else {
            req->params[idx].val = calloc(1, strlen(token) + 1);
            strncpy(req->params[idx].val, token, strlen(token));
            memset(token, 0, strlen(token));
		    idx++;
        }

	}

    free(buff);
    free(token);

	for (int i = 0; i < req->nParam; i++) {
		for(int j = 0; req->params[i].val[j]; j++){
			req->params[i].val[j] = tolower(req->params[i].val[j]);
		}
	}

}

// int main(void) {
//     char *recv = "*2\r\n$14\r\nECHO\r\n$3\r\nhey\r\n";
//     // char *recv = "*1\r\n$4\r\nping\r\n";
//     // char **items;
//     struct request *req;
//     req = malloc(sizeof(struct request));

//     parseReq(req, recv, strlen(recv));

//     printf("Number of params: %d\n", req->nParam);
//     for (int i = 0; i < req->nParam; i++) {
// 		printf("Len: %d, Val: %s\n", req->params[i].len, req->params[i].val);
// 	}

//     return 0;
// }