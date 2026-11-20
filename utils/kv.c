
#include "kv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define INITIAL_BUCKETS 16

// struct kv_entry {
//     struct kv_entry *next;
//     unsigned char *key;
//     size_t key_len;
//     unsigned char *value;
//     size_t value_len;
// };

struct kv_entry {
    struct kv_entry *next;

    unsigned char *key;
    size_t key_len;

    unsigned char *value;
    size_t value_len;

    bool has_expiry;
    uint64_t expire_at_ms;
};

struct kv_store {
    struct kv_entry **buckets;
    size_t bucket_count;
    size_t count;

    pthread_mutex_t mutex;
};

static void kv_lock(KV *store) {
    if (pthread_mutex_lock(&store->mutex) != 0) {
        abort();
    }
}

static void kv_unlock(KV *store) {
    if (pthread_mutex_unlock(&store->mutex) != 0) {
        abort();
    }
}

static unsigned char *copy_bytes(
    const void *source, size_t len
) {
    unsigned char *copy = malloc(len == 0 ? 1 : len);

    if (copy != NULL && len != 0)
        memcpy(copy, source, len);

    return copy;
}

static size_t bucket_index(
    const void *key, size_t len, size_t capacity
) {
    const unsigned char *bytes = key;
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }

    return (size_t)(hash % capacity);
}

static bool same_key(
    const struct kv_entry *entry,
    const void *key, size_t key_len
) {
    return entry->key_len == key_len &&
        (key_len == 0 ||
         memcmp(entry->key, key, key_len) == 0);
}

KV *kv_create(void) {
    KV *store = calloc(1, sizeof *store);
    if (store == NULL) return NULL;

    store->buckets = calloc(
        INITIAL_BUCKETS, sizeof *store->buckets
    );

    if (store->buckets == NULL) {
        free(store);
        return NULL;
    }

    store->bucket_count = INITIAL_BUCKETS;

    if (pthread_mutex_init(&store->mutex, NULL) != 0) {
        free(store->buckets);
        free(store);
        return NULL;
    }

    return store;
}

void kv_destroy(KV *store) {
    if (store == NULL) return;

    for (size_t i = 0; i < store->bucket_count; i++) {
        struct kv_entry *entry = store->buckets[i];

        while (entry != NULL) {
            struct kv_entry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }

    if (pthread_mutex_destroy(&store->mutex) != 0) {
        abort();
    }

    free(store->buckets);
    free(store);
}

size_t kv_count(KV *store) {
    if (store == NULL)
        return 0;

    kv_lock(store);

    size_t count = store->count;

    kv_unlock(store);
    return count;
}

static bool grow(KV *store) {
    if (store->bucket_count >
        SIZE_MAX / (2 * sizeof *store->buckets))
        return false;

    size_t new_count = store->bucket_count * 2;

    struct kv_entry **new_buckets =
        calloc(new_count, sizeof *new_buckets);

    if (new_buckets == NULL) return false;

    for (size_t i = 0; i < store->bucket_count; i++) {
        struct kv_entry *entry = store->buckets[i];

        while (entry != NULL) {
            struct kv_entry *next = entry->next;

            size_t index = bucket_index(
                entry->key, entry->key_len, new_count
            );

            entry->next = new_buckets[index];
            new_buckets[index] = entry;
            entry = next;
        }
    }

    free(store->buckets);
    store->buckets = new_buckets;
    store->bucket_count = new_count;

    return true;
}

static enum kv_status kv_set_internal(
    KV *store,
    const void *key, size_t key_len,
    const void *value, size_t value_len,
    bool has_expiry,
    uint64_t expire_at_ms
) {
    if (store == NULL ||
        (key == NULL && key_len != 0) ||
        (value == NULL && value_len != 0))
        return KV_INVALID;

    size_t index = bucket_index(
        key, key_len, store->bucket_count
    );

    // Replace an existing value.
    for (struct kv_entry *entry = store->buckets[index];
         entry != NULL; entry = entry->next) {

        if (same_key(entry, key, key_len)) {
            unsigned char *replacement =
                copy_bytes(value, value_len);

            if (replacement == NULL)
                return KV_NO_MEMORY;

            free(entry->value);
            entry->value = replacement;
            entry->value_len = value_len;
            entry->has_expiry = has_expiry;
            entry->expire_at_ms = expire_at_ms;

            return KV_OK;
        }
    }

    // Allocate a new entry.
    struct kv_entry *entry = calloc(1, sizeof *entry);
    if (entry == NULL) return KV_NO_MEMORY;

    entry->key = copy_bytes(key, key_len);
    entry->value = copy_bytes(value, value_len);

    if (entry->key == NULL || entry->value == NULL) {
        free(entry->key);
        free(entry->value);
        free(entry);
        return KV_NO_MEMORY;
    }

    entry->key_len = key_len;
    entry->value_len = value_len;
    entry->has_expiry = has_expiry;
    entry->expire_at_ms = expire_at_ms;

    // Grow once load reaches 75%.
    if (store->count >=
        store->bucket_count - store->bucket_count / 4) {

        if (!grow(store)) {
            free(entry->key);
            free(entry->value);
            free(entry);
            return KV_NO_MEMORY;
        }
    }

    // Recalculate after a possible resize.
    index = bucket_index(
        key, key_len, store->bucket_count
    );

    entry->next = store->buckets[index];
    store->buckets[index] = entry;
    store->count++;

    return KV_OK;
}


enum kv_status kv_set(
    KV *store,
    const void *key, size_t key_len,
    const void *value, size_t value_len
) {
    if (store == NULL)
        return KV_INVALID;

    kv_lock(store);

    enum kv_status status = kv_set_internal(
        store,
        key, key_len,
        value, value_len,
        false, 0
    );

    kv_unlock(store);
    return status;
}

enum kv_status kv_set_with_ttl(
    KV *store,
    const void *key, size_t key_len,
    const void *value, size_t value_len,
    uint64_t now_ms,
    uint64_t ttl_ms
) {
    // Match Redis PX semantics: TTL must be positive.
    if (ttl_ms == 0)
        return KV_INVALID;

    // Prevent unsigned integer overflow.
    if (ttl_ms > UINT64_MAX - now_ms)
        return KV_INVALID;

    if (store == NULL)
        return KV_INVALID;

    kv_lock(store);

    enum kv_status status = kv_set_internal(
        store,
        key, key_len,
        value, value_len,
        true, now_ms + ttl_ms
    );

    kv_unlock(store);
    return status;
}

static bool kv_remove_unlocked(
    KV *store,
    const void *key,
    size_t key_len
);

static enum kv_status kv_get_unlocked(
    KV *store,
    const void *key, size_t key_len,
    uint64_t now_ms,
    struct kv_blob *out
){
    if (out == NULL) return KV_INVALID;
    *out = (struct kv_blob){0};

    if (store == NULL || (key == NULL && key_len != 0))
        return KV_INVALID;

    size_t index = bucket_index(
        key, key_len, store->bucket_count
    );

    for (const struct kv_entry *entry = store->buckets[index];
         entry != NULL; entry = entry->next) {

        if (same_key(entry, key, key_len)) {
            if (entry->has_expiry &&
                now_ms >= entry->expire_at_ms) {

                kv_remove_unlocked(store, key, key_len);
                return KV_NOT_FOUND;
            }

            unsigned char *copy =
                copy_bytes(entry->value, entry->value_len);

            if (copy == NULL) return KV_NO_MEMORY;

            out->data = copy;
            out->len = entry->value_len;

            return KV_OK;
        }
    }

    return KV_NOT_FOUND;
}


enum kv_status kv_get(
    KV *store,
    const void *key, size_t key_len,
    uint64_t now_ms,
    struct kv_blob *out
) {
    if (store == NULL) {
        if (out != NULL)
            *out = (struct kv_blob){0};

        return KV_INVALID;
    }

    kv_lock(store);

    enum kv_status status = kv_get_unlocked(
        store, key, key_len, now_ms, out
    );

    kv_unlock(store);
    return status;
}


static bool kv_remove_unlocked(
    KV *store,
    const void *key,
    size_t key_len
) {
    if (store == NULL || (key == NULL && key_len != 0))
        return false;

    size_t index = bucket_index(
        key, key_len, store->bucket_count
    );

    struct kv_entry **slot = &store->buckets[index];

    while (*slot != NULL) {
        struct kv_entry *entry = *slot;

        if (same_key(entry, key, key_len)) {
            *slot = entry->next;

            free(entry->key);
            free(entry->value);
            free(entry);

            store->count--;
            return true;
        }

        slot = &entry->next;
    }

    return false;
}


bool kv_remove(
    KV *store,
    const void *key,
    size_t key_len
) {
    if (store == NULL)
        return false;

    kv_lock(store);

    bool removed =
        kv_remove_unlocked(store, key, key_len);

    kv_unlock(store);
    return removed;
}


void kv_blob_free(struct kv_blob *blob) {
    if (blob == NULL) return;

    free(blob->data);
    *blob = (struct kv_blob){0};
}
