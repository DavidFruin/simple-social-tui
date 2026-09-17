#ifndef TUI_NET_H
#define TUI_NET_H

#include "app.h"

/* The one place the TUI blocks.
 *
 * Every call here paints a status message and flushes it to the screen
 * before handing control to the library, which runs a synchronous
 * curl_easy_perform (30s timeout; 120s on the media paths). The UI is
 * frozen for that window -- no keys, no redraw -- so the "Loading..."
 * line is the only warning the user gets, and it has to land first.
 *
 * Keeping all of it behind this seam means a future move to a worker
 * thread touches these functions and nothing else. */

int net_refresh_feed(app_t *app);   /* replaces the feed with page 1 */
int net_load_more_feed(app_t *app); /* appends the next page */
int net_refresh_badge(app_t *app);  /* unseen notification count */

/* Post detail */
int net_open_post(app_t *app, const char *post_id, int feed_src);
int net_load_more_comments(app_t *app);
int net_toggle_like(app_t *app);

/* Writes */
int net_create_post(app_t *app, const char *text, const char *media_path);
int net_create_comment(app_t *app, const char *text);
int net_delete_post(app_t *app, const char *post_id);
int net_delete_comment(app_t *app, int comment_id);
int net_reload_comments(app_t *app);

#endif
