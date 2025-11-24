# HTTPS Proxy Makefile

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

# OpenSSL related and pthread for multi-threading, zlib for gzip decompression (This is no longer used in most recent versions)
LDFLAGS = -lssl -lcrypto -lpthread -lnsl -lz

proxy: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) proxy

