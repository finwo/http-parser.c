

http-parser-test: test.c src/http-parser.c
	$(CC) -o $@ $^

.PHONY: check
check: http-parser-test
	./$<
