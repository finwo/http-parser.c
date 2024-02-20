# http-parser

Small http message parsing library. Keep in mind that this library only handles
parsing the http request into a method, headers and body.

## Dependencies

This library makes use of [dep](https://github.com/finwo/dep) to manage it's
dependencies and exports.

- [finwo/asprintf][finwo/asprintf]
- [finwo/mindex][finwo/mindex]
- [finwo/str_extra][finwo/str_extra]
- [tidwall/buf][tidwall/buf]

## API

### Structs

<details>
  <summary>struct http_parser_event</summary>

  ```c
  struct http_parser_event {
    struct http_parser_message *request;
    struct http_parser_message *response;
    struct http_parser_pair *pair;
    struct buf *chunk;
    void *udata;
  };
  ```

  Represents an event triggered by the http-parser, like a request being
  detected and ready for processing by the user.

  The `request` of the event represents the http request that the event relates
  to. Same for the `response` on the event in relation to the response that is
  detected. The `pair` is simply a wrapper around these two entities.

  The `udata` is pulled directly from the pair.
</details>

<details>
  <summary>struct http_parser_message</summary>

  ```c
  struct http_parser_message {
    int ready;
    int status;
    char *statusMessage;
    char *method;
    char *path;
    char *query;
    char *version;
    struct mindex_t *meta;
    struct buf *body;
    struct buf *buf;
    int chunksize;
    int _state;
    void (*onChunk)(struct http_parser_event*);
    void *udata;
  };
  ```

  Represents an http message, can be either a request or a response and
  formatted as such.
</details>

<details>
  <summary>struct http_parser_pair</summary>

  ```c
  struct http_parser_pair {
    struct http_parser_message *request;
    struct http_parser_message *response;
    void *udata;
    void (*onRequest)(struct http_parser_event*);
    void (*onResponse)(struct http_parser_event*);
  };
  ```

  Simply a wrapper around 2 http_parser_message entities and container for the
  onRequest and onResponse callbacks
</details>

### Methods

<details>
  <summary>http_parser_pair_init(udata)</summary>

  ```c
  struct http_parser_pair    * http_parser_pair_init(void *udata);
  ```

  Initializes a pair of messages stored as request and response you can use to
  send data into that you want to parse and handle requests or responses on.
</details>

<details>
  <summary>http_parser_request_init()</summary>

  ```c
  struct http_parser_message * http_parser_request_init();
  ```

  Initializes an http_parser_message as a request to be used for parsing or
  rendering an http request.
</details>

<details>
  <summary>http_parser_response_init()</summary>

  ```c
  struct http_parser_message * http_parser_response_init();
  ```

  Initializes an http_parser_message as a response to be used for parsing or
  rendering an http response.
</details>


<details>
  <summary>http_parser_meta_get(subject,key)</summary>

  ```c
  const char * http_parser_meta_get(struct http_parser_message *subject, const char *key);
  ```

  Fetches a metadata value by the given key from the http_parser_message.

  **DO NOT** call `free()` on the returned `char*`, it's a pointer directly into
  the meta map.
</details>

<details>
  <summary>http_parser_meta_set(subject,key,value)</summary>

  ```c
  void http_parser_meta_set(struct http_parser_message *subject, const char *key, const char *value);
  ```

  Sets a metadata value on the given key on the subject. Makes a copy of both
  the key and the value, so it's still the user's responsibility to free these
  pointers if needed.
</details>

<details>
  <summary>http_parser_meta_del(subject,key)</summary>

  ```c
  void http_parser_meta_del(struct http_parser_message *subject, const char *key);
  ```

  Deletes a metadata value on the given key from the subject.
</details>

<details>
  <summary>http_parser_header_get(subject,key)</summary>

  ```c
  const char * http_parser_header_get(struct http_parser_message *subject, const char *key);
  ```

  Wrapper around `http_parser_meta_get`, prefixing the key with `header:`
</details>

<details>
  <summary>http_parser_header_set(subject,key,value)</summary>

  ```c
  void http_parser_header_set(struct http_parser_message *subject ,const char *key, const char *value);
  ```

  Wrapper around `http_parser_meta_set`, prefixing the key with `header:`
</details>

<details>
  <summary>http_parser_header_del(subject,key)</summary>

  ```c
  void http_parser_header_del(struct http_parser_message *subject, const char *key);
  ```

  Wrapper around `http_parser_meta_del`, prefixing the key with `header:`
</details>

<details>
  <summary>http_parser_request_data(request,data)</summary>

  ```c
  void http_parser_request_data(struct http_parser_message *request, const struct buf *data);
  ```

  Ingests data to parse as a request. Sets the `ready` field to 1 once a
  complete request has been detected and parsed.
</details>

<details>
  <summary>http_parser_response_data(response,data)</summary>

  ```c
  void http_parser_response_data(struct http_parser_message *response, const struct buf *data);
  ```

  Ingests data to parse as a response. Sets the `ready` field to 1 once a
  complete response has been detected and parsed.
</details>

<details>
  <summary>http_parser_pair_request_data(pair,data)</summary>

  ```c
  void http_parser_pair_request_data(struct http_parser_pair *pair, const struct buf *data);
  ```

  Ingests data to parse as a request on the pair's request using
  http_parser_request_data, and calls the onRequest callback once a complete
  request has been detected.
</details>

<details>
  <summary>http_parser_pair_response_data(pair,data)</summary>

  ```c
  void http_parser_pair_response_data(struct http_parser_pair *pair, const struct buf *data);
  ```

  Ingests data to parse as a response on the pair's response using
  http_parser_response_data, and calls the onResponse callback once a complete
  response has been detected.
</details>

<details>
  <summary>http_parser_message_free(subject)</summary>

  ```c
  void http_parser_message_free(struct http_parser_message *subject);
  ```

  Handles freeing of the allocated memory of a single http-parser message.
</details>

<details>
  <summary>http_parser_pair_free(pair)</summary>

  ```c
  void http_parser_pair_free(struct http_parser_pair *pair);
  ```

  Handles freeing of the allocated memory of a http-parser message pair,
  including it's request and response, excluding user-data.
</details>

<details>
  <summary>http_parser_status_message(status)</summary>

  ```c
  const char * http_parser_status_message(int status);
  ```

  Returns the default status message for a given status number.
</details>

<details>
  <summary>http_parser_sprintt_response(response)</summary>

  ```c
  struct buf * http_parser_sprint_response(struct http_parser_message *response);
  ```

  Returns a buffer representing the response as http response.
</details>

<details>
  <summary>http_parser_sprint_request(request)</summary>

  ```c
  struct buf * http_parser_sprint_request(struct http_parser_message *request);
  ```

  Returns a buffer representing the request as http request.
</details>

<details>
  <summary>http_parser_sprint_pair_response(pair)</summary>

  ```c
  struct buf * http_parser_sprint_pair_response(struct http_parser_pair *pair);
  ```

  Calls `http_parser_sprint_response` on the pair's response.
</details>

<details>
  <summary>http_parser_sprint_pair_request(pair)</summary>

  ```c
  struct buf * http_parser_sprint_pair_request(struct http_parser_pair *pair);
  ```

  Calls `http_parser_sprint_request` on the pair's request.
</details>

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

[finwo/asprintf]: https://github.com/finwo/c-asprintf
[finwo/mindex]: https://github.com/finwo/c-mindex
[finwo/str_extra]: https://github.com/finwo/c-strextra
[tidwall/buf]: https://github.com/tidwall/buf.c
