#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ncurses.h>
#include "shell.h"
#include "ss_config.h"

int shell_media_url(const char *media_path, char *out, size_t n) {
    if (!media_path || !*media_path) return -1;

    if (strncmp(media_path, "http://", 7) == 0 ||
        strncmp(media_path, "https://", 8) == 0) {
        snprintf(out, n, "%s", media_path);
        return 0;
    }

    /* base_url is the API endpoint (".../api.php"); media is served from
     * the same origin, so cut everything after the host. */
    const char *base = config_get_base_url();
    if (!base || !*base) return -1;

    const char *scheme = strstr(base, "://");
    if (!scheme) return -1;
    const char *host = scheme + 3;
    const char *slash = strchr(host, '/');

    size_t origin_len = slash ? (size_t)(slash - base) : strlen(base);
    if (origin_len >= n) return -1;

    char origin[512];
    if (origin_len >= sizeof(origin)) return -1;
    memcpy(origin, base, origin_len);
    origin[origin_len] = '\0';

    snprintf(out, n, "%s%s%s", origin, media_path[0] == '/' ? "" : "/", media_path);
    return 0;
}

int shell_open_url(const char *url) {
    if (!url || !*url) return -1;

    /* Give the terminal back while the opener runs, in case it spawns
     * something that wants the screen. */
    def_prog_mode();
    endwin();

    pid_t pid = fork();
    if (pid < 0) {
        reset_prog_mode();
        refresh();
        return -1;
    }

    if (pid == 0) {
        /* Keep the opener's chatter off the restored screen. */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    reset_prog_mode();
    refresh();

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) return -1;  /* no opener */
    return 0;
}

/* First of these that exists, when neither $VISUAL nor $EDITOR is set. */
static const char *EDITOR_FALLBACKS[] = { "nvim", "vim", "nano", "vi", NULL };

static const char *pick_editor(void) {
    const char *e = getenv("VISUAL");
    if (e && *e) return e;
    e = getenv("EDITOR");
    if (e && *e) return e;

    for (int i = 0; EDITOR_FALLBACKS[i]; i++) {
        char probe[256];
        snprintf(probe, sizeof(probe), "command -v %s >/dev/null 2>&1", EDITOR_FALLBACKS[i]);
        if (system(probe) == 0) return EDITOR_FALLBACKS[i];
    }
    return NULL;
}

int shell_edit_text(const char *initial, char *out, size_t outsz) {
    const char *editor = pick_editor();
    if (!editor) return -1;

    char path[] = "/tmp/ss-compose-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return -1;

    if (initial && *initial) {
        size_t n = strlen(initial);
        if (write(fd, initial, n) != (ssize_t)n) { close(fd); unlink(path); return -1; }
    }
    close(fd);

    /* Full handover: the editor owns the terminal until it exits. */
    def_prog_mode();
    endwin();

    pid_t pid = fork();
    if (pid < 0) {
        reset_prog_mode();
        refresh();
        unlink(path);
        return -1;
    }

    if (pid == 0) {
        /* Through a shell, so $EDITOR may carry arguments ("code -w"). */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "%s \"$1\"", editor);
        execlp("sh", "sh", "-c", cmd, "sh", path, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    reset_prog_mode();
    refresh();

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) { unlink(path); return -1; }

    FILE *f = fopen(path, "r");
    if (!f) { unlink(path); return -1; }

    size_t used = fread(out, 1, outsz - 1, f);
    out[used] = '\0';
    fclose(f);
    unlink(path);

    /* Editors add a trailing newline; an otherwise empty file means cancel. */
    while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == '\r'))
        out[--used] = '\0';

    return used > 0 ? 1 : 0;
}
