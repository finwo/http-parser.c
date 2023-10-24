#include <stdio.h>
#include <string.h>

#include "http-parser.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define T_RED "\e[31m"
#define T_LIME "\e[32m"
#define T_MAGENTA "\e[35m"
#define T_NORMAL "\e[00m"
#define T_BOLD "\e[01m"

#define ASSERT(M,c) (printf(((err|=!(c),(c)) ? (T_LIME " PASS " T_NORMAL " %s\n") : (T_RED " FAIL " T_NORMAL " %s\n")),M))

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

/* static void onRequest(struct http_parser_event *ev) { */
/*   // The request has been received */
/*   // Answer the request directly or pass it to a route handler of sorts */

/*   // Fetching the request */
/*   // Has been wrapped in http_parser_event to support more features in the future */
/*   struct http_parser_request *request = ev->request; */

/*   // Basic http request data */
/*   printf("Method: %s\n", request->method); */

/*   // Reading headers are case-insensitive due to non-compliant clients/servers */
/*   printf("Host:   %s\n", http_parser_header_get(request, "host")); */

/*   // Once you're done with the request, you'll have to free it */
/*   http_parser_request_free(request); */
/* } */

/* // Initialize a request */
/* struct http_parser_request *request = http_parser_init(); */

/* // Assign userdata into the request */
/* // Use it to track whatever you need */
/* request->udata = (void*)...; */

/* // Attach a function */

// Stored http messages
char *getMessage =
  "GET /foobar HTTP/1.1\r\n"
  "Host: localhost\r\n"
  "\r\n"
;

char *postMessage =
  "POST /foobar HTTP/1.1\r\n"
  "Host: localhost\r\n"
  "Content-Length: 13\r\n"
  "\r\n"
  "Hello World\r\n"
;

char *postChunkedMessage =
  "POST /foobar?token=pizza HTTP/1.1\r\n"
  "Host: localhost\r\n"
  "Transfer-Encoding: chunked\r\n"
  "\r\n"
  "2\r\n"
  "He\r\n"
  "B\r\n"
  "llo World\r\n"
  "0\r\n"
;

char *optionsRequest =
  "OPTIONS /hello/world HTTP/1.1\r\n"
  "Host: localhost\r\n"
  "\r\n"
;

char *responseMessage =
  "HTTP/1.0 200 OK\r\n"
  "Content-Length: 13\r\n"
  "\r\n"
  "Hello World\r\n"
;

char *responseChunkedMessage =
  "HTTP/1.0 200 OK\r\n"
  "Transfer-Encoding: chunked\r\n"
  "\r\n"
  "2\r\n"
  "He\r\n"
  "B\r\n"
  "llo World\r\n"
  "0\r\n"
;

char *responseNotFoundMessage =
  "HTTP/1.0 404 Not Found\r\n"
  "Content-Length: 11\r\n"
  "\r\n"
  "Not Found\r\n"
;


/* // Passing network data into it */
/* http_parser_request_data(request, message, strlen(message)); */

int main() {
  struct http_parser_message *request  = http_parser_request_init();
  struct http_parser_message *response = http_parser_response_init();
  struct buf *msgbuf;

  int err = 0;

  printf("# Pre-loaded request\n");
  ASSERT("request->method is null", request->method == NULL);
  ASSERT("request->body is null", request->body == NULL);
  ASSERT("request->chunksize is -1", request->chunksize == -1);

  http_parser_message_free(request);
  request  = http_parser_request_init();
  http_parser_request_data(request, &((struct buf){
    .data = getMessage,
    .len  = strlen(getMessage),
    .cap  = strlen(getMessage)
  }));

  printf("# GET request\n");
  ASSERT("request->version is 1.1", strcmp(request->version, "1.1") == 0);
  ASSERT("request->method is GET", strcmp(request->method, "GET") == 0);
  ASSERT("request->path is /foobar", strcmp(request->path, "/foobar") == 0);
  msgbuf = http_parser_sprint_request(request);
  ASSERT("request->toString matches", strcmp(getMessage, msgbuf->data) == 0);

  http_parser_message_free(request);
  request  = http_parser_request_init();
  http_parser_request_data(request, &((struct buf){
    .data = postMessage,
    .len  = strlen(postMessage),
    .cap  = strlen(postMessage)
  }));

  printf("# POST request\n");
  ASSERT("request->version is 1.1", strcmp(request->version, "1.1") == 0);
  ASSERT("request->method is POST", strcmp(request->method, "POST") == 0);
  ASSERT("request->path is /foobar", strcmp(request->path, "/foobar") == 0);
  ASSERT("request->body is \"Hello World\\r\\n\"", strcmp(request->body->data, "Hello World\r\n") == 0);
  msgbuf = http_parser_sprint_request(request);
  ASSERT("request->toString matches", strcmp(postMessage, msgbuf->data) == 0);

  http_parser_message_free(request);
  request  = http_parser_request_init();
  http_parser_request_data(request, &((struct buf){
    .data = postChunkedMessage,
    .len  = strlen(postChunkedMessage),
    .cap  = strlen(postChunkedMessage)
  }));

  printf("# POST request (chunked)\n");
  ASSERT("request->version is 1.1", strcmp(request->version, "1.1") == 0);
  ASSERT("request->method is POST", strcmp(request->method, "POST") == 0);
  ASSERT("request->path is /foobar", strcmp(request->path, "/foobar") == 0);
  ASSERT("request->body is \"Hello World\\r\\n\"", strcmp(request->body->data, "Hello World\r\n") == 0);

  http_parser_message_free(request);
  request = http_parser_request_init();
  http_parser_request_data(request, &((struct buf){
    .data = optionsRequest,
    .len  = strlen(optionsRequest),
    .cap  = strlen(optionsRequest)
  }));

  printf("# OPTIONS request\n");
  ASSERT("request->version is 1.1", strcmp(request->version, "1.1") == 0);
  ASSERT("request->method is OPTIONS", strcmp(request->method, "OPTIONS") == 0);
  ASSERT("request->path is /hello/world", strcmp(request->path, "/hello/world") == 0);

  printf("# Pre-loaded response\n");
  ASSERT("response->status = 200", response->status == 200);

  http_parser_message_free(response);
  response = http_parser_response_init();
  http_parser_response_data(response, &((struct buf){
    .data = responseMessage,
    .len  = strlen(responseMessage),
    .cap  = strlen(responseMessage)
  }));

  printf("# 200 OK response\n");
  ASSERT("response->status = 200", response->status == 200);
  ASSERT("response->statusmessage = \"OK\"", strcmp(response->statusMessage, "OK") == 0);
  ASSERT("response->body = \"Hello World\\r\\n\"", strcmp(response->body->data, "Hello World\r\n") == 0);
  ASSERT("response->toString matches", strcmp(responseMessage, http_parser_sprint_response(response)->data) == 0);

  http_parser_message_free(response);
  response = http_parser_response_init();
  http_parser_response_data(response, &((struct buf){
    .data = responseChunkedMessage,
    .len  = strlen(responseChunkedMessage),
    .cap  = strlen(responseChunkedMessage)
  }));

  printf("# 200 OK response (chunked)\n");
  ASSERT("response->status = 200", response->status == 200);
  ASSERT("response->statusmessage = \"OK\"", strcmp(response->statusMessage, "OK") == 0);

  printf("resonse->body: %s\n", response->body->data);

  ASSERT("response->body = \"Hello World\\r\\n\"", strcmp(response->body->data, "Hello World\r\n") == 0);

  http_parser_message_free(response);
  response = http_parser_response_init();
  http_parser_response_data(response, &((struct buf){
    .data = responseNotFoundMessage,
    .len  = strlen(responseNotFoundMessage),
    .cap  = strlen(responseNotFoundMessage)
  }));

  printf("# 404 Not Found response\n");
  ASSERT("response->status = 404", response->status == 404);
  ASSERT("response->statusmessage = \"Not Found\"", strcmp(response->statusMessage, "Not Found") == 0);
  ASSERT("response->body = \"Not Found\\r\\n\"", strcmp(response->body->data, "Not Found\r\n") == 0);
  ASSERT("response->toString matches", strcmp(responseNotFoundMessage, http_parser_sprint_response(response)->data) == 0);

  return err;
}
