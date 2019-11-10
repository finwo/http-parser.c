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
  struct http_parser_message *request = ev->request;

  // Basic http request data
  printf("Method: %s\n", request->method);

  // Reading headers are case-insensitive due to non-compliant clients/servers
  printf("Host:   %s\n", http_parser_header_get(request, "host"));

  // Once you're done with the request, you'll have to free it yourself
  http_parser_message_free(request);

  // Or you can free the whole pair
  http_parser_pair_free(ev->pair);
}

// Initialize a request/response pair
struct http_parser_pair *reqres = http_parser_pair_init();

// Userdata to be included can be assigned
reqres->udata = (void*)...;

// Trigger function 'onRequest' when the request is ready
reqres->onRequest = onRequest;

// Stored http message
char *message =
  "GET / HTTP/1.1\r\n"
  "Host: localhost\r\n"
  "\r\n"
;

// Passing network data into it
http_parser_pair_request_data(reqseq, message, strlen(message));
```
