#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cfg.h"
#include "ss_utils.h"

#define DEFAULT_INI \
"# Simple Social TUI config\n" \
"\n" \
"[compose]\n" \
"# inline   = edit posts and comments in a box inside the TUI (default)\n" \
"# external = suspend the TUI and open $EDITOR instead\n" \
"editor = inline\n" \
"\n" \
"[refresh]\n" \
"# seconds a view may sit before it re-fetches on its own; 0 disables\n" \
"feed_seconds = 60\n" \
"badge_seconds = 60\n" \
"\n" \
"[feed]\n" \
"# posts fetched per page by load-more\n" \
"page_size = 25\n"

static void set_defaults(tui_cfg_t *cfg) {
    cfg->compose_mode = CFG_COMPOSE_INLINE;
    cfg->feed_refresh_secs = 60;
    cfg->badge_refresh_secs = 60;
    cfg->page_size = 25;
    cfg->path[0] = '\0';
}

/* Write a commented default file the first time, so the knobs are
 * discoverable without reading the source. Failure is not fatal. */
static void seed_default(const char *dir, const char *path) {
    mkdir(dir, 0755);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(DEFAULT_INI, f);
    fclose(f);
}

static void apply(tui_cfg_t *cfg, const char *key, const char *val) {
    if (strcmp(key, "editor") == 0) {
        cfg->compose_mode = (strcmp(val, "external") == 0)
            ? CFG_COMPOSE_EXTERNAL : CFG_COMPOSE_INLINE;
    } else if (strcmp(key, "feed_seconds") == 0) {
        int n = atoi(val);
        if (n >= 0) cfg->feed_refresh_secs = n;
    } else if (strcmp(key, "badge_seconds") == 0) {
        int n = atoi(val);
        if (n >= 0) cfg->badge_refresh_secs = n;
    } else if (strcmp(key, "page_size") == 0) {
        int n = atoi(val);
        if (n > 0 && n <= 200) cfg->page_size = n;
    }
}

void tui_cfg_load(tui_cfg_t *cfg) {
    set_defaults(cfg);

    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    char dir[400];
    snprintf(dir, sizeof(dir), "%s/.config", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.config/simple-social-tui", home);

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    snprintf(cfg->path, sizeof(cfg->path), "%s", path);

    FILE *f = fopen(path, "r");
    if (!f) {
        seed_default(dir, path);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = str_trim(line);
        if (*s == '#' || *s == ';' || *s == '\0' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        apply(cfg, str_trim(s), str_trim(eq + 1));
    }
    fclose(f);
}
