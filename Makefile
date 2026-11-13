CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

all: deco640d

deco640d: deco640d.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f deco640d deco640d.log

.PHONY: all clean
