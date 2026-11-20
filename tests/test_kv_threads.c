
#include "../utils/kv.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define THREAD_COUNT 4
#define KEYS_PER_THREAD 500

struct worker_args {
    KV *store;
    int id;
};

static void *worker(void *arg) {
    struct worker_args *args = arg;

    for (int i = 0; i < KEYS_PER_THREAD; i++) {
        char key[64];
        char value[64];

        int key_len = snprintf(
            key, sizeof key, "thread-%d-key-%d",
            args->id, i
        );

        int value_len = snprintf(
            value, sizeof value, "value-%d", i
        );

        assert(key_len > 0);
        assert((size_t)key_len < sizeof key);
        assert(value_len > 0);
        assert((size_t)value_len < sizeof value);

        assert(kv_set(
            args->store,
            key, (size_t)key_len,
            value, (size_t)value_len
        ) == KV_OK);

        struct kv_blob result = {0};

        assert(kv_get(
            args->store,
            key, (size_t)key_len,
            0, &result
        ) == KV_OK);

        assert(result.len == (size_t)value_len);
        assert(memcmp(
            result.data, value, result.len
        ) == 0);

        kv_blob_free(&result);
    }

    return NULL;
}

int main(void) {
    KV *store = kv_create();
    assert(store != NULL);

    pthread_t threads[THREAD_COUNT];
    struct worker_args args[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; i++) {
        args[i] = (struct worker_args){
            .store = store,
            .id = i
        };

        assert(pthread_create(
            &threads[i], NULL, worker, &args[i]
        ) == 0);
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }

    assert(kv_count(store) ==
           THREAD_COUNT * KEYS_PER_THREAD);

    kv_destroy(store);

    puts("Concurrent dictionary tests passed.");
    return 0;
}
