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

void cstore_init(comment_store_t *s) {
    s->comments = NULL;
    s->count = 0;
    s->cap = 0;
    s->has_more = 0;
    s->total_count = 0;
}

void cstore_free(comment_store_t *s) {
    free(s->comments);
    cstore_init(s);
}

void cstore_clear(comment_store_t *s) {
    s->count = 0;
    s->has_more = 0;
    s->total_count = 0;
}

int cstore_append(comment_store_t *s, const api_comment_t *src, int n) {
    if (n <= 0) return 0;

    if (s->count + n > s->cap) {
        int cap = s->cap ? s->cap : 32;
        while (cap < s->count + n) cap *= 2;
        api_comment_t *c = realloc(s->comments, (size_t)cap * sizeof(api_comment_t));
        if (!c) return -1;
        s->comments = c;
        s->cap = cap;
    }

    memcpy(s->comments + s->count, src, (size_t)n * sizeof(api_comment_t));
    s->count += n;
    return 0;
}

void nstore_init(notif_store_t *s) {
    s->notifs = NULL;
    s->count = 0;
    s->cap = 0;
    s->has_more = 0;
}

void nstore_free(notif_store_t *s) {
    free(s->notifs);
    nstore_init(s);
}

void nstore_clear(notif_store_t *s) {
    s->count = 0;
    s->has_more = 0;
}

int nstore_append(notif_store_t *s, const api_notification_t *src, int n) {
    if (n <= 0) return 0;

    if (s->count + n > s->cap) {
        int cap = s->cap ? s->cap : 32;
        while (cap < s->count + n) cap *= 2;
        api_notification_t *p = realloc(s->notifs, (size_t)cap * sizeof(api_notification_t));
        if (!p) return -1;
        s->notifs = p;
        s->cap = cap;
    }

    memcpy(s->notifs + s->count, src, (size_t)n * sizeof(api_notification_t));
    s->count += n;
    return 0;
}
