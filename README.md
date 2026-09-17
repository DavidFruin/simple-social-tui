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
| `c` | write a post |
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
| `c` | write a comment |
| `l` | like / unlike |
| `o` | open the post's media in the system viewer (`xdg-open`) |
| `d` | delete the selected comment (yours only) |
| `D` | delete the post (yours only) |
| `Esc` / `Backspace` | back to the feed, selection preserved |

In the composer:

| Key | Does |
|---|---|
| typing | inserts; `Enter` starts a new line |
| `Ctrl-D` | send |
| `Esc` | cancel — asks first if you have written something |
| arrows, `Home` / `End`, `Ctrl-A` / `Ctrl-E` | move the cursor |
| `Backspace` / `Delete` | delete a character either side of the cursor |

`Ctrl-D` rather than `Ctrl-Enter` to send, because terminals almost universally
report `Ctrl-Enter` as a plain `Enter` — there is no reliable way to tell them
apart, and `Enter` is needed for new lines.

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
src/editor.c    inline multi-line editor
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

## Composing

Inline by default: a box over the current screen with wrapping, a live character
counter against the server's 5000 limit, and cursor movement that works per
character rather than per byte, so editing text with accents, CJK or emoji behaves.
Input goes through `get_wch()` rather than `getch()`, which hands back one byte at a
time and would happily insert half a character.

Set `editor = external` in the config to suspend the TUI and use your editor
instead. Since `$VISUAL` and `$EDITOR` are both commonly unset, it falls back to
the first of `nvim`, `vim`, `nano`, `vi` that is installed. Saving an empty file
counts as cancelling. The command runs through `sh -c` so `$EDITOR` may carry
arguments (`code -w`), with the path passed as `"$1"` rather than interpolated.

If the TUI is killed outright while the editor is open, its temp file in `/tmp` is
left behind — the cleanup runs after the editor exits.

## Status

Working: feed with expand-on-selection, load-more paging, idle refresh that holds
your position, notification badge, live resize, 16-color theming that inherits the
terminal's scheme, post detail with paged comments and comment selection, like /
unlike, opening media in the system viewer, writing posts and comments (inline or
`$EDITOR`), deleting your own posts and comments with confirmation, help overlay,
error modals.

Not built yet: media attach, notifications, users, profiles, settings,
login / register.

Deletes are guarded twice: the key does nothing but explain itself unless the post
or comment is yours, and then it asks. `ui_confirm` treats anything other than
`y` as a decline, so a stray keypress cannot confirm a delete.
