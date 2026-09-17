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
