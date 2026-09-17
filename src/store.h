#ifndef TUI_STORE_H
#define TUI_STORE_H

#include "ss_api.h"

/* Growable post list.
 *
 * The library's api_posts_result_t is a fixed api_post_t[256] -- roughly
 * 1.4 MB, since every post carries a 5000-byte text buffer. It is a
 * per-call transport buffer, not somewhere to accumulate a feed, so pages
 * get copied out of it into here and it never lives on the stack. */

typedef struct {
    api_post_t *posts;
    int count;
    int cap;
    int has_more;
    int total_count;
} post_store_t;

void store_init(post_store_t *s);
void store_free(post_store_t *s);
void store_clear(post_store_t *s);
int  store_append(post_store_t *s, const api_post_t *src, int n);

/* Same treatment for comments: api_comments_result_t is a fixed
 * api_comment_t[256], also carrying 5000-byte text buffers. */
typedef struct {
    api_comment_t *comments;
    int count;
    int cap;
    int has_more;
    int total_count;
} comment_store_t;

void cstore_init(comment_store_t *s);
void cstore_free(comment_store_t *s);
void cstore_clear(comment_store_t *s);
int  cstore_append(comment_store_t *s, const api_comment_t *src, int n);

#endif
