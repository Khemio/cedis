
#ifndef CEDIS_KV_H
#define CEDIS_KV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct kv_store KV;

struct kv_blob {
    unsigned char *data;
    size_t len;
};

enum kv_status {
    KV_OK,
    KV_NOT_FOUND,
    KV_NO_MEMORY,
    KV_INVALID
};

KV *kv_create(void);
void kv_destroy(KV *store);
size_t kv_count(KV *store);

enum kv_status kv_set(
    KV *store,
    const void *key, size_t key_len,
    const void *value, size_t value_len
);

enum kv_status kv_get(
    KV *store,
    const void *key, size_t key_len,
    uint64_t now_ms,
    struct kv_blob *out
);

enum kv_status kv_set_with_ttl(
    KV *store,
    const void *key, size_t key_len,
    const void *value, size_t value_len,
    uint64_t now_ms,
    uint64_t ttl_ms
);

bool kv_remove(
    KV *store,
    const void *key, size_t key_len
);

void kv_blob_free(struct kv_blob *blob);

#endif
