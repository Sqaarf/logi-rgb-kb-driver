CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic
PREFIX  ?= /usr/local

BIN  := gpro-rgb
SRC  := $(wildcard src/*.c)
OBJ  := $(SRC:src/%.c=build/%.o)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c src/gpro.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

install-udev:
	install -Dm644 udev/70-logitech-gpro.rules $(DESTDIR)/etc/udev/rules.d/70-logitech-gpro.rules
	udevadm control --reload-rules && udevadm trigger

# Per-user unit that re-applies ~/.config/gpro-rgb/profile on plug-in and at login.
install-service:
	install -Dm644 systemd/gpro-rgb-restore.service $(HOME)/.config/systemd/user/gpro-rgb-restore.service
	systemctl --user daemon-reload
	systemctl --user enable gpro-rgb-restore.service

clean:
	rm -rf build $(BIN)

.PHONY: all install install-udev install-service clean
