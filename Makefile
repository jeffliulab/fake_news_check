# CS112 Final Project Part1 - HTTPS Proxy Makefile
# Based on a2 Makefile with modifications for proxy

src = $(wildcard *.c)
obj = $(src:.c=.o)
CC = gcc

CFLAGS = -Dfscanf=BANNED_fscanf \
         -Dscanf=BANNED_scanf \
         -Dgets=BANNED_gets \
         -Dputs=BANNED_puts \
         -Dputchar=BANNED_putchar \
         -Dgetchar=BANNED_getchar \
         -Dfgetc=BANNED_fgetc \
         -Dfputc=BANNED_fputc \
         -Dfputs=BANNED_fputs

# OpenSSL related and pthread for multi-threading, zlib for gzip decompression
LDFLAGS = -lssl -lcrypto -lpthread -lnsl -lz

proxy: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) proxy

