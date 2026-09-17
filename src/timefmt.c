#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "timefmt.h"

#define RELATIVE_CUTOFF (24 * 60 * 60)

/* Parsed as local time, which is what the server's timestamps have matched
 * so far. If that ever drifts, this is the one place to fix it. */
static int parse_ts(const char *ts, time_t *out) {
    if (!ts || !*ts) return -1;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (!strptime(ts, "%Y-%m-%d %H:%M:%S", &tm)) return -1;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return -1;
    *out = t;
    return 0;
}

void timefmt_short(const char *ts, char *out, size_t n) {
    time_t t;
    if (parse_ts(ts, &t) != 0) {
        snprintf(out, n, "%s", ts ? ts : "");
        return;
    }

    double diff = difftime(time(NULL), t);
    if (diff < 0) diff = 0;

    if (diff < RELATIVE_CUTOFF) {
        int secs = (int)diff;
        if (secs < 60)        snprintf(out, n, "%ds", secs);
        else if (secs < 3600) snprintf(out, n, "%dm", secs / 60);
        else                  snprintf(out, n, "%dh", secs / 3600);
    } else {
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(out, n, "%Y-%m-%d", &lt);
    }
}

void timefmt_full(const char *ts, char *out, size_t n) {
    time_t t;
    if (parse_ts(ts, &t) != 0) {
        snprintf(out, n, "%s", ts ? ts : "");
        return;
    }
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(out, n, "%Y-%m-%d %H:%M", &lt);
}
