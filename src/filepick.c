#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ncurses.h>
#include "filepick.h"
#include "ui.h"
#include "ss_config.h"

/* Extensions the server accepts, grouped by the limit it applies.
 * media.php: image 10MB, video 100MB, audio 50MB. */
static const char *IMAGE_EXT[] = { "jpg", "jpeg", "png", "gif", "webp", NULL };
static const char *VIDEO_EXT[] = { "mov", "mp4", "m4v", "webm", NULL };
static const char *AUDIO_EXT[] = { "wav", "mp3", NULL };

#define MB (1024L * 1024L)

static const char *ext_of(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && dot[1] ? dot + 1 : "";
}

static int in_list(const char *const *list, const char *ext) {
    for (int i = 0; list[i]; i++)
        if (strcasecmp(list[i], ext) == 0) return 1;
    return 0;
}

const char *filepick_kind(const char *name) {
    const char *e = ext_of(name);
    if (in_list(IMAGE_EXT, e)) return "image";
    if (in_list(VIDEO_EXT, e)) return "video";
    if (in_list(AUDIO_EXT, e)) return "audio";
    return NULL;
}

int filepick_is_media(const char *name) {
    return filepick_kind(name) != NULL;
}

long filepick_max_bytes(const char *name) {
    const char *k = filepick_kind(name);
    if (!k) return 0;
    if (strcmp(k, "image") == 0) return 10 * MB;
    if (strcmp(k, "video") == 0) return 100 * MB;
    return 50 * MB;
}

/* ---------- directory listing ---------- */

typedef struct {
    char name[256];
    int  is_dir;
    long size;
} entry_t;

static entry_t *g_entries = NULL;
static int g_count = 0;
static int g_cap = 0;

static int cmp_entries(const void *a, const void *b) {
    const entry_t *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;   /* dirs first */
    return strcasecmp(x->name, y->name);
}

static void entries_clear(void) { g_count = 0; }

static entry_t *entry_push(void) {
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 128;
        entry_t *p = realloc(g_entries, (size_t)cap * sizeof(entry_t));
        if (!p) return NULL;
        g_entries = p;
        g_cap = cap;
    }
    return &g_entries[g_count++];
}

/* Lists dirs and accepted media files; everything else is hidden, since
 * anything else is not attachable anyway. */
static int list_dir(const char *path) {
    entries_clear();

    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") == 0) continue;
        if (de->d_name[0] == '.' && strcmp(de->d_name, "..") != 0) continue;

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !filepick_is_media(de->d_name)) continue;

        entry_t *e = entry_push();
        if (!e) break;
        snprintf(e->name, sizeof(e->name), "%s", de->d_name);
        e->is_dir = is_dir;
        e->size = is_dir ? 0 : (long)st.st_size;
    }
    closedir(d);

    qsort(g_entries, (size_t)g_count, sizeof(entry_t), cmp_entries);
    return 0;
}

static void human_size(long bytes, char *out, size_t n) {
    if (bytes >= MB) snprintf(out, n, "%.1f MB", (double)bytes / MB);
    else if (bytes >= 1024) snprintf(out, n, "%ld KB", bytes / 1024);
    else snprintf(out, n, "%ld B", bytes);
}

static void normalise(char *path) {
    /* Collapse a trailing "/.." by dropping the last component. */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';

    char *tail = strrchr(path, '/');
    if (tail && strcmp(tail, "/..") == 0) {
        *tail = '\0';
        char *prev = strrchr(path, '/');
        if (prev) *(prev == path ? prev + 1 : prev) = '\0';
        else snprintf(path, 2, "/");
    }
    if (!path[0]) snprintf(path, 2, "/");
}

/* ---------- typed path with completion ---------- */

/* Completes to the longest common prefix of the matches in the entered
 * directory. Returns how many candidates matched. */
static int complete_path(char *buf, size_t bufsz) {
    char dir[1024], frag[256];

    const char *slash = strrchr(buf, '/');
    if (!slash) {
        snprintf(dir, sizeof(dir), ".");
        snprintf(frag, sizeof(frag), "%s", buf);
    } else {
        size_t dlen = (size_t)(slash - buf);
        if (dlen == 0) { snprintf(dir, sizeof(dir), "/"); }
        else {
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, buf, dlen);
            dir[dlen] = '\0';
        }
        snprintf(frag, sizeof(frag), "%s", slash + 1);
    }

    DIR *d = opendir(dir);
    if (!d) return 0;

    char common[256] = {0};
    int matches = 0;
    int only_dir = 0;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (de->d_name[0] == '.' && frag[0] != '.') continue;
        if (strncmp(de->d_name, frag, strlen(frag)) != 0) continue;

        char full[1280];
        snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
        struct stat st;
        int isdir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);

        /* Only offer things that could lead to an attachable file. */
        if (!isdir && !filepick_is_media(de->d_name)) continue;

        if (matches == 0) {
            snprintf(common, sizeof(common), "%s", de->d_name);
            only_dir = isdir;
        } else {
            for (size_t i = 0; common[i]; i++) {
                if (de->d_name[i] != common[i]) { common[i] = '\0'; break; }
            }
            only_dir = 0;
        }
        matches++;
    }
    closedir(d);

    if (matches == 0) return 0;

    if (slash) {
        size_t dlen = (size_t)(slash - buf) + 1;
        char head[1024];
        if (dlen >= sizeof(head)) dlen = sizeof(head) - 1;
        memcpy(head, buf, dlen);
        head[dlen] = '\0';
        snprintf(buf, bufsz, "%s%s", head, common);
    } else {
        snprintf(buf, bufsz, "%s", common);
    }

    /* A single directory match: add the slash so the next Tab descends. */
    if (matches == 1 && only_dir) {
        size_t l = strlen(buf);
        if (l + 2 < bufsz && buf[l - 1] != '/') { buf[l] = '/'; buf[l + 1] = '\0'; }
    }

    return matches;
}

/* Single-line path prompt. Returns 1 if a path was entered. */
static int path_prompt(app_t *app, char *out, size_t outsz, const char *start_dir) {
    char buf[1100];
    size_t sd = start_dir ? strlen(start_dir) : 0;
    snprintf(buf, sizeof(buf), "%s%s",
             sd ? start_dir : "/",
             (sd && start_dir[sd - 1] == '/') ? "" : "/");
    size_t len = strlen(buf);
    char note[128] = {0};

    curs_set(1);
    timeout(-1);

    for (;;) {
        int w = COLS - 8;
        if (w > 72) w = 72;
        if (w < 24) w = COLS > 24 ? 24 : COLS;
        int h = 5;
        int top = (LINES - h) / 2, left = (COLS - w) / 2;
        if (top < 0) top = 0;
        if (left < 0) left = 0;

        ui_draw(app);

        WINDOW *win = newwin(h, w, top, left);
        if (!win) break;

        box(win, 0, 0);
        wattron(win, A_BOLD);
        mvwaddstr(win, 0, 2, " Path to attach ");
        wattroff(win, A_BOLD);

        /* Show the tail of the path when it is longer than the box. */
        int avail = w - 4;
        const char *shown = buf;
        if ((int)len > avail) shown = buf + (len - avail);
        mvwaddstr(win, 1, 2, shown);

        wattron(win, A_DIM);
        mvwaddstr(win, h - 2, 2, note[0] ? note : "tab completes   enter attaches   esc cancels");
        wattroff(win, A_DIM);

        int cx = 2 + ((int)len > avail ? avail : (int)len);
        if (cx > w - 2) cx = w - 2;
        wmove(win, 1, cx);
        wrefresh(win);

        int ch = wgetch(win);
        delwin(win);
        note[0] = '\0';

        if (ch == 27) { curs_set(0); timeout(500); return 0; }

        if (ch == '\r' || ch == '\n' || ch == KEY_ENTER) {
            if (len == 0) continue;
            curs_set(0);
            timeout(500);
            snprintf(out, outsz, "%s", buf);
            return 1;
        }

        if (ch == '\t') {
            int m = complete_path(buf, sizeof(buf));
            len = strlen(buf);
            if (m == 0) snprintf(note, sizeof(note), "no matches");
            else if (m > 1) snprintf(note, sizeof(note), "%d matches", m);
            continue;
        }

        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) buf[--len] = '\0';
            continue;
        }

        if (ch == 21) { len = 0; buf[0] = '\0'; continue; }   /* ^U */

        if (ch >= 32 && ch < 127 && len + 1 < sizeof(buf)) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }

    curs_set(0);
    timeout(500);
    return 0;
}

/* ---------- browser ---------- */

int filepick_run(app_t *app, char *out, size_t outsz) {
    char dir[1024];

    /* Start where downloads land, per the library's config. */
    config_t cfg;
    config_load(&cfg);
    if (cfg.download_dir[0]) snprintf(dir, sizeof(dir), "%s", cfg.download_dir);
    else {
        const char *home = getenv("HOME");
        snprintf(dir, sizeof(dir), "%s", home ? home : "/");
    }

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        const char *home = getenv("HOME");
        snprintf(dir, sizeof(dir), "%s", home ? home : "/");
    }

    if (list_dir(dir) != 0) {
        app_set_error(app, "cannot read %s", dir);
        return 0;
    }

    int sel = 0, top = 0;
    char note[160] = {0};

    app->input_active = 1;
    timeout(-1);

    for (;;) {
        int w = COLS - 6;
        if (w > 80) w = 80;
        if (w < 30) w = COLS > 30 ? 30 : COLS;

        int h = LINES - 4;
        if (h > 20) h = 20;
        if (h < 8) h = LINES > 8 ? 8 : LINES;

        int rows = h - 4;
        if (rows < 1) rows = 1;

        int top_y = (LINES - h) / 2, left = (COLS - w) / 2;
        if (top_y < 0) top_y = 0;
        if (left < 0) left = 0;

        if (sel < 0) sel = 0;
        if (sel >= g_count) sel = g_count ? g_count - 1 : 0;
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        if (top < 0) top = 0;

        ui_draw(app);

        WINDOW *win = newwin(h, w, top_y, left);
        if (!win) break;

        box(win, 0, 0);

        char title[256];
        ui_utf8_take(title, sizeof(title), dir, w - 8, 1);
        wattron(win, A_BOLD);
        mvwprintw(win, 0, 2, " %s ", title);
        wattroff(win, A_BOLD);

        if (g_count == 0) {
            wattron(win, A_DIM);
            mvwaddstr(win, 1, 2, "no folders or media files here");
            wattroff(win, A_DIM);
        }

        for (int i = 0; i < rows && top + i < g_count; i++) {
            entry_t *e = &g_entries[top + i];
            int y = 1 + i;
            int selected = (top + i == sel);

            char label[512];
            if (e->is_dir) {
                snprintf(label, sizeof(label), "%s/", e->name);
            } else {
                char sz[32];
                human_size(e->size, sz, sizeof(sz));
                int too_big = e->size > filepick_max_bytes(e->name);
                snprintf(label, sizeof(label), "%s  (%s%s)",
                         e->name, sz, too_big ? ", too large" : "");
            }

            char shown[512];
            ui_utf8_take(shown, sizeof(shown), label, w - 4, 1);

            if (selected) wattron(win, COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
            else if (e->is_dir) wattron(win, COLOR_PAIR(CP_AUTHOR));
            else if (e->size > filepick_max_bytes(e->name)) wattron(win, COLOR_PAIR(CP_ERROR));

            mvwhline(win, y, 1, ' ', w - 2);
            mvwaddstr(win, y, 2, shown);

            if (selected) wattroff(win, COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
            else if (e->is_dir) wattroff(win, COLOR_PAIR(CP_AUTHOR));
            else if (e->size > filepick_max_bytes(e->name)) wattroff(win, COLOR_PAIR(CP_ERROR));
        }

        wattron(win, A_DIM);
        char hint[256];
        ui_utf8_take(hint, sizeof(hint),
                     note[0] ? note : "enter open/attach   h up   / type a path   esc cancel",
                     w - 4, 1);
        mvwaddstr(win, h - 2, 2, hint);
        wattroff(win, A_DIM);

        wrefresh(win);

        int ch = wgetch(win);
        delwin(win);
        note[0] = '\0';

        switch (ch) {
            case 27:                       /* esc */
                app->input_active = 0;
                timeout(500);
                return 0;

            case 'j': case KEY_DOWN: sel++; break;
            case 'k': case KEY_UP:   sel--; break;
            case KEY_NPAGE: sel += rows; break;
            case KEY_PPAGE: sel -= rows; break;
            case 'g': case KEY_HOME: sel = 0; break;
            case 'G': case KEY_END:  sel = g_count - 1; break;

            case 'h': case KEY_LEFT: {
                char up[1024];
                snprintf(up, sizeof(up), "%s/..", dir);
                normalise(up);
                if (list_dir(up) == 0) {
                    snprintf(dir, sizeof(dir), "%s", up);
                    sel = 0; top = 0;
                }
                break;
            }

            case '/': {
                char typed[1024];
                if (path_prompt(app, typed, sizeof(typed), dir)) {
                    struct stat ts;
                    if (stat(typed, &ts) != 0) {
                        snprintf(note, sizeof(note), "no such file: %.100s", typed);
                    } else if (S_ISDIR(ts.st_mode)) {
                        if (list_dir(typed) == 0) {
                            snprintf(dir, sizeof(dir), "%s", typed);
                            sel = 0; top = 0;
                        }
                    } else if (!filepick_is_media(typed)) {
                        snprintf(note, sizeof(note), "not an accepted media type");
                    } else if (ts.st_size > filepick_max_bytes(typed)) {
                        snprintf(note, sizeof(note), "too large: %s files are capped at %ld MB",
                                 filepick_kind(typed), filepick_max_bytes(typed) / MB);
                    } else {
                        app->input_active = 0;
                        timeout(500);
                        snprintf(out, outsz, "%s", typed);
                        return 1;
                    }
                }
                timeout(-1);
                break;
            }

            case '\r': case '\n': case KEY_ENTER: {
                if (g_count == 0) break;
                entry_t *e = &g_entries[sel];

                if (e->is_dir) {
                    char next[1024];
                    snprintf(next, sizeof(next), "%s/%s", dir, e->name);
                    normalise(next);
                    if (list_dir(next) == 0) {
                        snprintf(dir, sizeof(dir), "%s", next);
                        sel = 0; top = 0;
                    } else {
                        snprintf(note, sizeof(note), "cannot open %.100s", e->name);
                    }
                    break;
                }

                if (e->size > filepick_max_bytes(e->name)) {
                    snprintf(note, sizeof(note), "too large: %s files are capped at %ld MB",
                             filepick_kind(e->name), filepick_max_bytes(e->name) / MB);
                    break;
                }

                app->input_active = 0;
                timeout(500);
                snprintf(out, outsz, "%s/%s", dir, e->name);
                return 1;
            }

            default: break;
        }
    }

    app->input_active = 0;
    timeout(500);
    return 0;
}
