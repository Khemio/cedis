
#include "../utils/kv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_value(
    KV *store,
    const void *key, size_t key_len,
    const void *expected, size_t expected_len
) {
    struct kv_blob out = {0};

    assert(kv_get(store, key, key_len, 0, &out) == KV_OK);
    assert(out.len == expected_len);

    assert(expected_len == 0 ||
           memcmp(out.data, expected, expected_len) == 0);

    kv_blob_free(&out);
}

int main(void) {
    KV *store = kv_create();
    assert(store != NULL);

    // Binary key and value.
    const unsigned char key[] = {'u', '\0', 's'};
    unsigned char value[] = {'A', '\0', 'B'};

    assert(kv_set(
        store, key, sizeof key, value, sizeof value
    ) == KV_OK);

    // Modifying the original must not modify storage.
    value[0] = 'X';

    expect_value(store, key, sizeof key, "A\0B", 3);
    assert(kv_count(store) == 1);

    // Replacement should not increase key count.
    assert(kv_set(
        store, key, sizeof key, "new", 3
    ) == KV_OK);

    expect_value(store, key, sizeof key, "new", 3);
    assert(kv_count(store) == 1);

    // Empty key and empty value.
    assert(kv_set(store, NULL, 0, NULL, 0) == KV_OK);
    expect_value(store, NULL, 0, NULL, 0);
    assert(kv_count(store) == 2);

    // Deletion.
    assert(kv_remove(store, key, sizeof key));
    assert(kv_count(store) == 1);

    struct kv_blob missing = {0};

    assert(kv_get(
        store, key, sizeof key, 0, &missing
    ) == KV_NOT_FOUND);

    assert(missing.data == NULL);

    // Force several table resizes.
    for (int i = 0; i < 1200; i++) {
        char k[32], v[32];

        int kl = snprintf(k, sizeof k, "key-%d", i);
        int vl = snprintf(v, sizeof v, "value-%d", i);

        assert(kl > 0 && vl > 0);

        assert(kv_set(
            store, k, (size_t)kl, v, (size_t)vl
        ) == KV_OK);
    }

    assert(kv_count(store) == 1201);

    // All entries must survive resizing.
    for (int i = 0; i < 1200; i++) {
        char k[32], v[32];

        int kl = snprintf(k, sizeof k, "key-%d", i);
        int vl = snprintf(v, sizeof v, "value-%d", i);

        expect_value(
            store, k, (size_t)kl, v, (size_t)vl
        );
    }


    // Expiration after 50 ms, starting at t = 1000.
    assert(kv_set_with_ttl(
        store, "temp", 4,
        "abc", 3,
        1000, 50
    ) == KV_OK);

    struct kv_blob result = {0};

    // Still valid one millisecond before expiration.
    assert(kv_get(
        store, "temp", 4, 1049, &result
    ) == KV_OK);

    assert(result.len == 3);
    assert(memcmp(result.data, "abc", 3) == 0);
    kv_blob_free(&result);

    // Expires exactly at t = 1050.
    assert(kv_get(
        store, "temp", 4, 1050, &result
    ) == KV_NOT_FOUND);

    assert(result.data == NULL);
    assert(kv_count(store) == 1201);

    // Replacing a key without TTL removes its expiration.
    assert(kv_set_with_ttl(
        store, "reset", 5,
        "old", 3,
        2000, 10
    ) == KV_OK);

    assert(kv_set(
        store, "reset", 5,
        "new", 3
    ) == KV_OK);

    assert(kv_get(
        store, "reset", 5, 3000, &result
    ) == KV_OK);

    assert(result.len == 3);
    assert(memcmp(result.data, "new", 3) == 0);
    kv_blob_free(&result);

    assert(kv_remove(store, "reset", 5));

    // Zero TTL is invalid.
    assert(kv_set_with_ttl(
        store, "x", 1, "y", 1,
        1000, 0
    ) == KV_INVALID);

    // Reject timestamp overflow.
    assert(kv_set_with_ttl(
        store, "x", 1, "y", 1,
        UINT64_MAX - 2, 3
    ) == KV_INVALID);


    kv_destroy(store);

    puts("All binary-safe dictionary tests passed.");
    return 0;
}
