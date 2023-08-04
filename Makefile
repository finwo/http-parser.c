SRC=$(wildcard src/*.c)
SRC+=test.c

override CFLAGS?=-Wall -s -O2

include lib/.dep/config.mk

http-parser-test: $(SRC) src/http-parser-statusses.h src/http-parser.h
	$(CC) -Isrc $(INCLUDES) $(CFLAGS) -o $@ $(SRC)

.PHONY: check
check: http-parser-test
	./$<

.PHONY: clean
clean:
	rm -f http-parser-test
