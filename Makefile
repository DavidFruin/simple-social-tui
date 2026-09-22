CC = gcc

# Vendored as a git submodule rather than a sibling checkout, so this repo
# is self-contained: `git clone --recursive` + `make` is the whole story,
# no separate simple-social-cli clone required.
CLI_DIR   = vendor/simple-social-cli
LIBSS     = $(CLI_DIR)/lib/libss.a

# ncursesw (wide char) so UTF-8 glyphs in the feed render correctly.
NCURSES_CFLAGS = $(shell pkg-config --cflags ncursesw 2>/dev/null || echo -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600)
NCURSES_LIBS   = $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw -ltinfo)

CFLAGS = -Wall -Wextra -O2 -I$(CLI_DIR)/lib $(NCURSES_CFLAGS)
# Static archive linked directly (not -lss + rpath): the built binary ends
# up a single self-contained file that works wherever it's copied or
# symlinked, e.g. onto PATH via `make install`. -lcurl still needs
# vendor/simple-social-cli's own vendor/ dir on the search path, since
# that's where its vendor-links step puts the libcurl.so symlink dev
# packages don't always provide.
LDFLAGS = $(LIBSS) -L$(CLI_DIR)/vendor -lcurl $(NCURSES_LIBS)

SRCS = src/main.c src/app.c src/ui.c src/net.c src/store.c src/timefmt.c src/cfg.c \
       src/detail.c src/shell.c src/editor.c src/filepick.c \
       src/settings.c src/auth.c
OBJS = $(SRCS:.c=.o)
BIN  = simple-social-tui

all: check-lib $(BIN)

check-lib:
	@if [ ! -f $(CLI_DIR)/Makefile ]; then \
		echo "error: $(CLI_DIR) is empty - the submodule wasn't checked out."; \
		echo "Run: git submodule update --init --recursive"; \
		exit 1; \
	fi
	@if [ ! -f $(LIBSS) ]; then \
		echo "Building vendored simple-social-cli library..."; \
		$(MAKE) -C $(CLI_DIR); \
	fi

$(BIN): $(OBJS) $(LIBSS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/sstui

uninstall:
	rm -f /usr/local/bin/sstui

.PHONY: all clean check-lib install uninstall
