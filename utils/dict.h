typedef struct dict *Dict;

typedef struct {
    char* value;
    long expire_at;
    int expiry;
} response;

/* create a new empty dictionary */
Dict DictCreate(void);

/* destroy a dictionary */
void DictDestroy(Dict);

/* insert a new key-value pair into an existing dictionary */
void DictInsert(Dict, const char *key, const char *value, int expiry, long expire_at);

/* return the most recently inserted value associated with a key */
/* or 0 if no matching key is present */
// const char *DictSearch(Dict, response *res, const char *key);
int DictSearch(Dict, response *res, const char *key);

int DictSize(Dict);

/* delete the most recently inserted record with the given key */
/* if there is no such record, has no effect */
void DictDelete(Dict, const char *key);