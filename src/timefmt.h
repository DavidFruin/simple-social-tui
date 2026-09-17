#ifndef TUI_TIMEFMT_H
#define TUI_TIMEFMT_H

#include <stddef.h>

/* Server timestamps look like "2026-04-13 22:17:00".
 * Under 24h old renders relative ("2h", "15m"); older renders absolute. */

void timefmt_short(const char *ts, char *out, size_t n);  /* list columns */
void timefmt_full(const char *ts, char *out, size_t n);   /* detail views */

#endif
