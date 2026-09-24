# Simple Social TUI

A full-screen terminal UI for Simple Social, built on ncurses.

Third front end built on the same library as the other two. `simple-social-cli`
is argv-style and scriptable, `simple-social-cli-interactive` is a wizard REPL, and
this one is a screen you drive with the keyboard. All three read and write the
same `~/.simple-social-cli/` session state, so logging in with any of them logs
you in everywhere, even though each repo now builds its own copy of the library.

`simple-social-cli` is vendored in as a git submodule under `vendor/`, not a
sibling checkout - this repo is self-contained. It never modifies the vendored
copy.

## Build

Install a compiler, the ncurses headers and the libcurl runtime. On Debian or
Ubuntu:

```
sudo apt install build-essential libncurses-dev libcurl4 pkg-config
```

(On Ubuntu 24.04 and later the runtime package is `libcurl4t64`, and apt picks
it if you ask for `libcurl4`.) `pkg-config` is optional, since the Makefile
falls back without it. libcurl's own dev package is not needed, because its
headers are vendored.

Then clone and build somewhere you can write to, such as your home directory:

```
cd ~
git clone --recursive https://github.com/DavidFruin/simple-social-tui.git
cd simple-social-tui && make
```

Don't clone from `/` or another root-owned directory. `git clone` fails there
with `could not create work tree dir ... Permission denied`. Using `sudo git
clone` instead leaves a root-owned tree that `make` can't write to either.

`make` builds the vendored `simple-social-cli` library automatically if it isn't
already built. If you cloned without `--recursive`, run
`git submodule update --init --recursive` first.

Running it needs a UTF-8 locale.

If `sudo apt update` fails because of a broken third-party repository (for
example a missing signing key), `sudo apt update && sudo apt install ...` never
reaches the install. Run the install on its own, or fix or remove that
repository under `/etc/apt/sources.list.d/`.

The built binary is statically linked against the vendored library (no `.so` to
keep track of), so it works wherever it ends up - copied, symlinked, whatever.
`sudo make install` puts it on your PATH as `sstui`; `sudo make uninstall`
removes it.

## Run

```
./simple-social-tui
```

Or, once installed: `sstui`, from anywhere.

If there's no session it shows a login screen: log in, register, or reset a
forgotten password. All three use the same shared token, so logging in here logs
you into the CLI tools too — and logging out of any of them logs you out of all
three.

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

In the users tab:

| Key | Does |
|---|---|
| `j` / `k`, `g` / `G` | move |
| `Enter` | open that person's profile |
| `r` | refresh |

In a profile (and the Me tab):

| Key | Does |
|---|---|
| `j` / `k` | move through their posts |
| `Enter` | open the selected post |
| `f` | follow or unfollow (not on your own profile) |
| `w` | people they follow |
| `W` | people following them |
| `Esc` | back |

In the settings tab:

| Key | Does |
|---|---|
| `j` / `k` | move between actions |
| `Enter` | run the selected action |

In the notifications tab:

| Key | Does |
|---|---|
| `j` / `k`, `g` / `G` | move |
| `Enter` | open the post it refers to; on load-more, fetch the next page |
| `r` | refresh |

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

In the file picker:

| Key | Does |
|---|---|
| `j` / `k`, `g` / `G` | move |
| `Enter` | enter a folder, or attach the file |
| `h` / `←` | up a folder |
| `/` | type a path instead, with `Tab` completion |
| `Esc` | cancel |

In the composer:

| Key | Does |
|---|---|
| typing | inserts; `Enter` starts a new line |
| `Ctrl-D` | send |
| `Ctrl-O` | attach a media file (posts only) |
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
src/filepick.c  media picker: folder browser and typed path with completion
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

## Account and session

The login screen covers logging in, registering (email a code, verify it, choose a
password) and resetting a forgotten password. On success the token is written to the
shared location, so the CLI tools pick it up.

Settings shows the live session and config, then the three account operations.
They sit there rather than at top level so they are reachable without being
somewhere you land by accident:

- **Change password** — emails a code to the address on file, verifies it, then takes
  the new password twice. It asks before sending anything.
- **Log out** — says plainly that this logs out the CLI tools too, since the token is
  shared. Clears the local token either way; the server call is best-effort.
- **Delete account** — the password typed twice and matching, then a final
  confirmation, then gone. The mismatch is caught locally, so a typo never reaches
  the server.

Passwords echo as dots and are wiped from their buffers after use rather than left
sitting in memory.

Logging out returns to the login screen rather than exiting, and everything fetched
for the old session is dropped first, so the next person never sees the last one's
feed.

## Screens

Tabs are the top level; posts, profiles and follows lists push on top of whatever
opened them, and `Esc` walks back. Frames carry enough to rebuild what they return
to, because opening a profile from a follows list overwrites the profile state
behind it — so popping back re-fetches rather than showing stale fields.

The feed and a profile's posts are the same list over different data, so they share
`post_list_t` and one renderer. The users tab and a follows list likewise share one.

The user list marks who you follow from a single `getMyFollows` call rather than an
`isFollowing` call per row.

`getMyFollows` and `getMyFollowers` return the literal string `"Unknown"` as the
timestamp for relationships predating the server's tracking, so those render blank
rather than as a date.

## Notifications

Opening the tab is what marks them seen, so the badge clears on arrival. The unseen
count is read *before* marking, and that many entries are flagged as new, since the
API reports only a count and a `last_seen` timestamp — there is no per-notification
seen flag to read.

`api.php` writes five types: `like`, `unlike`, `comment`, `follow` and `unfollow`.
Follows carry no post id, so `Enter` on one says so rather than opening nothing.

`getNotifications` serves fixed pages of 25 and reports no total, so "more" is
inferred from getting a full page back — which means the last page is reached by
asking once more and getting fewer than 25.

## Attaching media

`Ctrl-O` in the composer opens a picker rooted at the library's configured
`download_dir` (`~/Downloads`). It lists folders and accepted media only, since
nothing else can be attached, and shows each file's size. `/` switches to typing a
path, where `Tab` completes — fully on a unique match, to the common prefix
otherwise, reporting how many candidates matched.

In external-editor mode there is no box to host `Ctrl-O`, so it asks about
attaching after the editor exits.

What the server accepts, checked locally so an oversized file is refused before a
120-second upload rather than after:

| Kind | Extensions | Limit |
|---|---|---|
| image | jpg, jpeg, png, gif, webp | 10 MB |
| video | mov, mp4, m4v, webm | 100 MB |
| audio | wav, mp3 | 50 MB |

Posting with media uploads first, then creates the post. If the post fails after a
successful upload, the media is deleted again rather than left orphaned on the
server — the same rollback the CLI does.

Comments take no media: the API has no parameter for it.

## What has not been exercised

Some paths cannot be tested without consequences for real people or real accounts,
so they are written but unverified:

- **follow / unfollow** and **liking someone else's post** write a notification to
  that person's account. (Liking your own post is rejected by the server, so there is
  no harmless target.)
- **register** and **password reset** email a one-time code to a real address.
- **logout**, **change password** and **delete account** would end or alter the
  session this repo was developed against. Their confirmation gates are verified —
  including the delete mismatch check — but not the operations themselves.

Everything else in the Status list below was run against the live API.

## Status

Working: feed with expand-on-selection, load-more paging, idle refresh that holds
your position, notification badge, live resize, 16-color theming that inherits the
terminal's scheme, post detail with paged comments and comment selection, like /
unlike, opening media in the system viewer, writing posts and comments (inline or
`$EDITOR`), attaching media with a picker or a typed path, deleting your own posts
and comments with confirmation, help overlay, error modals.

Everything the CLI can do is now reachable here.

Deletes are guarded twice: the key does nothing but explain itself unless the post
or comment is yours, and then it asks. `ui_confirm` treats anything other than
`y` as a decline, so a stray keypress cannot confirm a delete.
