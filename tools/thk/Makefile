# SPDX-License-Identifier: GPL-2.0
# Makefile for THK userspace tools

CC ?= gcc
CFLAGS += -Wall -Wextra -O2 -I../../include
LDFLAGS +=

# THK CLI
THK_OBJS = thk.o thk_format.o
THK_BIN = thk

# THK Daemon
THKD_OBJS = thkd.o thk_config.o thk_llm.o
THKD_BIN = thkd

INSTALL ?= install
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SBINDIR ?= $(PREFIX)/sbin
CONFDIR ?= /etc/thk

all: $(THK_BIN) $(THKD_BIN)

$(THK_BIN): $(THK_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(THKD_BIN): $(THKD_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dependencies
thk.o: thk.c thk_common.h thk_format.h
thk_format.o: thk_format.c thk_format.h thk_common.h
thkd.o: thkd.c thk_common.h thk_config.h thk_llm.h
thk_config.o: thk_config.c thk_config.h thk_common.h
thk_llm.o: thk_llm.c thk_llm.h thk_config.h thk_common.h

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(SBINDIR)
	$(INSTALL) -d $(DESTDIR)$(CONFDIR)
	$(INSTALL) -d $(DESTDIR)/run/thk
	$(INSTALL) -m 755 $(THK_BIN) $(DESTDIR)$(BINDIR)/
	$(INSTALL) -m 755 $(THKD_BIN) $(DESTDIR)$(SBINDIR)/
	$(INSTALL) -m 644 config/thk.conf.example $(DESTDIR)$(CONFDIR)/
	@if [ ! -f $(DESTDIR)$(CONFDIR)/thk.conf ]; then \
		$(INSTALL) -m 640 config/thk.conf.example \
			$(DESTDIR)$(CONFDIR)/thk.conf; \
	fi

clean:
	$(RM) $(THK_BIN) $(THKD_BIN) *.o

.PHONY: all install clean
