#include <stdlib.h>
#include <string.h>
#include "store.h"

void store_init(post_store_t *s) {
    s->posts = NULL;
    s->count = 0;
    s->cap = 0;
    s->has_more = 0;
    s->total_count = 0;
}

void store_free(post_store_t *s) {
    free(s->posts);
    store_init(s);
}

void store_clear(post_store_t *s) {
    s->count = 0;
    s->has_more = 0;
    s->total_count = 0;
}

int store_append(post_store_t *s, const api_post_t *src, int n) {
    if (n <= 0) return 0;

    if (s->count + n > s->cap) {
        int cap = s->cap ? s->cap : 32;
        while (cap < s->count + n) cap *= 2;
        api_post_t *p = realloc(s->posts, (size_t)cap * sizeof(api_post_t));
        if (!p) return -1;
        s->posts = p;
        s->cap = cap;
    }

    memcpy(s->posts + s->count, src, (size_t)n * sizeof(api_post_t));
    s->count += n;
    return 0;
}
