
.PHONY: test
test: http-parser-test
	./$<

http-parser-test: test.c src/http-parser.c
	$(CC) -o $@ $^
