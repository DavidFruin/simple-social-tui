#ifndef TUI_CFG_H
#define TUI_CFG_H

/* TUI-only config, read from ~/.config/simple-social-tui/config.ini.
 * Separate from the shared library's own config (base_url, dirs), which
 * still lives under ~/.config/simple-social-cli/. */

#define CFG_COMPOSE_INLINE   0
#define CFG_COMPOSE_EXTERNAL 1

typedef struct {
    int  compose_mode;       /* inline editor, or hand off to $EDITOR */
    int  feed_refresh_secs;  /* 0 disables the idle refresh timer */
    int  badge_refresh_secs; /* 0 disables the unseen-count timer */
    int  page_size;          /* posts per load-more page */
    char path[512];          /* where the config was read from */
} tui_cfg_t;

void tui_cfg_load(tui_cfg_t *cfg);

#endif
