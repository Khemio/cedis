#include <unistd.h>

#ifndef PRASER_H
#define PRASER_H

// typedef struct param param;
// typedef struct request request;
struct param {
    int len;
    char* val;
};

struct request {
    int nParam;
    struct param* params;
};

void parseReq(struct request* req, const char* recv, size_t len);

#endif