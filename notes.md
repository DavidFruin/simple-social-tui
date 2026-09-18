# Notes

Working notes for the TUI. Anything here that turns into a real task should end up
in a commit or the README instead.

## Known bugs

### One-off segfault opening the Users tab (unreproduced)

**Seen:** 2026-09-17, while building the users/profiles work that became `dc008ca`.

**What happened:** the `-O2` build died on pressing `3` (Users tab). Run directly
without a tty it exited 139 and dumped core. `dmesg`:

```
simple-social-t[505432]: segfault at 0 ip 0000000000000000 sp 00007fff178db9b0
  error 14 likely on CPU 6 (core 10, socket 0)
Code: Unable to access opcode bytes at 0xffffffffffffffd6
```

Instruction pointer at 0 means it jumped to a null address rather than dereferencing
a bad pointer.

**Could not reproduce, across:**

- `-O0 -g` — users tab rendered fine
- `-O2 -g` under gdb in a pty — rendered all 17 users
- three consecutive plain `-O2` runs — all fine
- AddressSanitizer, exercising users → profile → follows → back → back → Me →
  follows → notifications → feed — no errors reported, survived the lot

**Probably not our bug.** The same run also printed "Not logged in", meaning
`getMyInfo` had failed, and the line immediately before it in `dmesg` was:

```
rtw_8821ce 0000:02:00.0: firmware failed to leave lps state
```

That is the wifi adapter failing to come out of power save. A network drop mid-call
explains the failed login, and plausibly a crash somewhere in libcurl's or the
library's error path — the media helpers had a use-after-free of exactly that kind
(fixed in `simple-social-cli`, commit message "Report media errors, and fix a
use-after-free in api_delete_media"), so the library's error paths are not above
suspicion.

**Unresolved, not fixed.** If it recurs, that is a real signal and worth chasing
properly. Useful next steps if it does:

- get a core with `ulimit -c unlimited` and a `core_pattern` that writes locally,
  then `gdb ./simple-social-tui core` for the frame that jumped to 0
- check whether it correlates with the network being down again, by pulling the
  cable / disabling wifi and opening the Users tab
- `api_get_users` and `api_get_my_follows` are the two library calls that run on
  that keypress, so they are where to look first
