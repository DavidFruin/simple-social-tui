CC = gcc

CLI_DIR   = ../simple-social-cli
LIBSS     = $(CLI_DIR)/lib/libss.so

# ncursesw (wide char) so UTF-8 glyphs in the feed render correctly.
NCURSES_CFLAGS = $(shell pkg-config --cflags ncursesw 2>/dev/null || echo -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600)
NCURSES_LIBS   = $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw -ltinfo)

CFLAGS  = -Wall -Wextra -O2 -I$(CLI_DIR)/lib $(NCURSES_CFLAGS)
LDFLAGS = -L$(CLI_DIR)/lib -lss $(NCURSES_LIBS) -Wl,-rpath,'$$ORIGIN/$(CLI_DIR)/lib'

SRCS = src/main.c src/app.c src/ui.c src/net.c src/store.c src/timefmt.c src/cfg.c
OBJS = $(SRCS:.c=.o)
BIN  = simple-social-tui

all: check-lib $(BIN)

check-lib:
	@if [ ! -f $(LIBSS) ]; then \
		echo "error: $(LIBSS) not found."; \
		echo "Build the shared library first: (cd $(CLI_DIR) && make)"; \
		exit 1; \
	fi

$(BIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(BIN)

.PHONY: all clean check-lib install uninstall
