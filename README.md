# http-parser

Small http message parsing library. Keep in mind that this library only handles
parsing the http request into a method, headers and body.

## Basic usage

```c
static void onRequest(struct http_parser_event *ev) {
  // The request has been received
  // Answer the request directly or pass it to a route handler of sorts

  // Fetching the request
  // Has been wrapped in http_parser_event to support more features in the future
  struct http_parser_request *request = ev->request;

  // Basic http request data
  printf("Method: %s\n", request->method);

  // Reading headers are case-insensitive due to non-compliant clients/servers
  printf("Host:   %s\n", http_parser_header_get(request, "host"));

  // Once you're done with the request, you'll have to free it
  http_parser_request_free(request);
}

// Initialize a request
struct http_parser_request *request = http_parser_init();

// Assign userdata into the request
// Use it to track whatever you need
request->udata = (void*)...;

// Attach a function

// Stored http message
char *message =
  "GET / HTTP/1.1\r\n"
  "Host: localhost\r\n"
  "\r\n"
;

// Passing network data into it
http_parser_request_data(request, message, strlen(message));
```
