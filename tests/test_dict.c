
#include "../utils/dict.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_value(
    Dict dict,
    const char *key,
    const char *expected
) {
    response res = {0};

    assert(DictSearch(dict, &res, key) == 1);
    assert(strcmp(res.value, expected) == 0);

    // DictSearch currently allocates a copy.
    free(res.value);
}

int main(void) {
    Dict dict = DictCreate();

    assert(DictSize(dict) == 0);

    // Test insertion.
    DictInsert(dict, "name", "Alice", 0, 0);

    assert(DictSize(dict) == 1);
    expect_value(dict, "name", "Alice");

    // Test replacement of an existing key.
    DictInsert(dict, "name", "Bob", 0, 0);

    expect_value(dict, "name", "Bob");
    assert(DictSize(dict) == 1);

    // Test deletion.
    DictDelete(dict, "name");

    assert(DictSize(dict) == 0);

    response res = {0};
    assert(DictSearch(dict, &res, "name") == 0);

    // Test resizing.
    for (int i = 0; i < 1200; i++) {
        char key[32];
        char value[32];

        snprintf(key, sizeof key, "key-%d", i);
        snprintf(value, sizeof value, "value-%d", i);

        DictInsert(dict, key, value, 0, 0);
    }

    assert(DictSize(dict) == 1200);

    for (int i = 0; i < 1200; i++) {
        char key[32];
        char value[32];

        snprintf(key, sizeof key, "key-%d", i);
        snprintf(value, sizeof value, "value-%d", i);

        expect_value(dict, key, value);
    }

    DictDestroy(dict);

    puts("All dictionary tests passed.");
    return 0;
}
