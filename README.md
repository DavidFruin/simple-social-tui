# Simple Social TUI

A full-screen terminal UI for Simple Social, built on ncurses.

Third front end over the same shared library as the other two. `simple-social-cli`
is argv-style and scriptable, `simple-social-cli-interactive` is a wizard REPL, and
this one is a screen you drive with the keyboard. All three link the same
`libss.so` and share one session, so logging in with any of them logs you in
everywhere.

This repo never modifies `simple-social-cli`.

## Build

```
cd ../simple-social-cli && make      # build the shared library first
cd ../simple-social-tui  && make
```

Needs `libncursesw` and a UTF-8 locale. `make` refuses to run with a clear message
if `../simple-social-cli/lib/libss.so` is missing.

## Run

```
./simple-social-tui
```

There's no login screen yet, so log in with a sibling tool first — the session is
shared:

```
(cd ../simple-social-cli && ./simple-social-cli login)
```

## Keys

Vim-style, with arrows and page keys working too.

| Key | Does |
|---|---|
| `j` / `k`, `↓` / `↑` | move the cursor; the selected post expands in place |
| `Ctrl-d` / `Ctrl-u`, `PgDn` / `PgUp` | half-page jumps |
| `g` / `G`, `Home` / `End` | first / last post |
| `Enter` / `Space` | open the selected post; on the load-more row, fetch the next page |
| `l` | like / unlike |
| `r` | refresh the current view |
| `1`–`5`, `Tab` / `Shift-Tab` | switch tabs |
| `?` | help overlay |
| `q` | quit |

In an open post:

| Key | Does |
|---|---|
| `j` / `k` | move between comments; above the first, scrolls the post body |
| `Enter` / `Space` | on the load-more row, fetch the next page of comments |
| `l` | like / unlike |
| `o` | open the post's media in the system viewer (`xdg-open`) |
| `Esc` / `Backspace` | back to the feed, selection preserved |

## Config

`~/.config/simple-social-tui/config.ini`, written with commented defaults on first
run. Separate from the library's own config (base URL, directories), which stays at
`~/.config/simple-social-cli/`.

```ini
[compose]
editor = inline        # or "external" to hand off to $EDITOR

[refresh]
feed_seconds = 60      # idle re-fetch; 0 disables
badge_seconds = 60     # unseen notification count; 0 disables

[feed]
page_size = 25
```

## How it blocks

The library is synchronous: `api_call` runs `curl_easy_perform` with a 30s timeout
(120s on media paths). This TUI leans into that rather than adding threads — every
fetch paints a status message and flushes it to the screen *before* handing control
to the library, and the UI is frozen until the call returns.

All of it sits behind `src/net.c`, so the day that stops being acceptable, a worker
thread changes those functions and nothing else. Two consequences worth knowing:
the idle refresh timer is suppressed while a composer or prompt has focus, and the
timers run off `getch()` waking every 500ms rather than from a signal.

## Layout

```
src/main.c      startup, session restore, ncurses init/teardown
src/app.c       app state, event loop, key handling, idle timers
src/ui.c        drawing: tab bar, feed, status line, modal, help, UTF-8 truncation
src/detail.c    post detail: post, comments, selection, scrolling
src/net.c       the blocking-fetch seam
src/shell.c     handing the terminal to an external program, media URL building
src/store.c     growable post and comment lists
src/timefmt.c   relative under 24h, absolute beyond
src/cfg.c       the config above
```

`api_posts_result_t` is a fixed `api_post_t[256]` — about 1.4 MB, since each post
carries a 5000-byte text buffer. It's a transport buffer, not storage: pages get
copied out of one reused heap instance into `store.c`, and it never touches the
stack.

## Status

Working: feed with expand-on-selection, load-more paging, idle refresh that holds
your position, notification badge, live resize, 16-color theming that inherits the
terminal's scheme, post detail with paged comments and comment selection, like /
unlike, opening media in the system viewer, help overlay, error modals.

Not built yet: compose (inline and `$EDITOR`), commenting, media attach, delete
post / comment, notifications, users, profiles, settings, login / register.

The detail view renders into a flat line list rather than walking
variable-height items, which makes scrolling and comment selection much easier to
get right. `src/shell.c` suspends and restores ncurses around an external program;
the optional `$EDITOR` composer will reuse it. Note `$EDITOR` is unset in this
environment, so that path will need a fallback chain.
