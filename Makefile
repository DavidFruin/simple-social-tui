CC = gcc

# Vendored as a git submodule rather than a sibling checkout, so this repo
# is self-contained: `git clone --recursive` + `make` is the whole story,
# no separate simple-social-cli clone required.
CLI_DIR   = vendor/simple-social-cli
LIBSS     = $(CLI_DIR)/lib/libss.a

# ncursesw (wide char) so UTF-8 glyphs in the feed render correctly.
NCURSES_CFLAGS = $(shell pkg-config --cflags ncursesw 2>/dev/null || echo -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600)
NCURSES_LIBS   = $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw -ltinfo)

# -MMD -MP emits .d files listing each object's header dependencies, so
# editing a header rebuilds everything that includes it. Without it a
# changed struct left stale objects linking against the old layout.
CFLAGS = -Wall -Wextra -O2 -MMD -MP -I$(CLI_DIR)/lib $(NCURSES_CFLAGS)
# Static archive linked directly (not -lss + rpath): the built binary ends
# up a single self-contained file that works wherever it's copied or
# symlinked, e.g. onto PATH via `make install`. libcurl is linked the same
# way vendor/simple-social-cli does it: pkg-config when the dev package is
# installed, else the libcurl.so.4 SONAME that the runtime package ships.
CURL_LIBS = $(shell pkg-config --libs libcurl 2>/dev/null || echo -l:libcurl.so.4)
LDFLAGS = $(LIBSS) $(CURL_LIBS) $(NCURSES_LIBS)

SRCS = src/main.c src/app.c src/ui.c src/net.c src/store.c src/timefmt.c src/cfg.c \
       src/detail.c src/shell.c src/editor.c src/filepick.c \
       src/settings.c src/auth.c
OBJS = $(SRCS:.c=.o)
BIN  = simple-social-tui

all: check-lib $(BIN)

# Always delegates to the submodule's own make rather than only building
# when the archive is missing: after a submodule bump the old archive is
# still sitting there, and skipping the build silently links stale code
# against fresh headers.
check-lib:
	@if [ ! -f $(CLI_DIR)/Makefile ]; then \
		echo "error: $(CLI_DIR) is empty - the submodule wasn't checked out."; \
		echo "Run: git submodule update --init --recursive"; \
		exit 1; \
	fi
	@$(MAKE) -C $(CLI_DIR)

$(BIN): $(OBJS) $(LIBSS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN) $(OBJS:.o=.d)

# Depends on `all`, not just $(BIN): $(BIN)'s own prerequisite $(LIBSS) has
# no file rule of its own, only check-lib's recursive submodule build. Going
# straight for `install` without a plain `make` first used to skip check-lib
# entirely and fail with "No rule to make target '.../lib/libss.a'".
install: all
	install -m 755 $(BIN) /usr/local/bin/sstui

uninstall:
	rm -f /usr/local/bin/sstui

-include $(OBJS:.o=.d)

.PHONY: all clean check-lib install uninstall
