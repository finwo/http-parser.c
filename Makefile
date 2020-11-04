http-parser-test: test.c src/http-parser.c
	$(CC) -Isrc -o $@ $^

.PHONY: check
check: http-parser-test
	./$<
