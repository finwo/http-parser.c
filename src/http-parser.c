#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "http-parser.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void http_parser_header_free(struct http_parser_header *header) {
  if (header->next) http_parser_header_free(header->next);
  if (header->key) free(header->key);
  if (header->value) free(header->value);
  free(header);
}

char *http_parser_header_get(struct http_parser_request *request, char *key) {
  struct http_parser_header *header = request->headers;
  while(header) {
    if (!strcasecmp(key, header->key)) {
      return header->value;
    }
    header = header->next;
  }
  return NULL;
}

void http_parser_request_free(struct http_parser_request *request) {
  if (request->method) free(request->method);
  if (request->path) free(request->path);
  if (request->version) free(request->version);
  if (request->body) free(request->body);
  if (request->headers) http_parser_header_free(request->headers);
  free(request);
}

struct http_parser_request * http_parser_request_init() {
  struct http_parser_request *request = calloc(1, sizeof(struct http_parser_request));
  return request;
}

void http_parser_request_data(struct http_parser_request *request, char *data, int size) {
  struct http_parser_header *header;
  struct http_parser_event *ev;
  char *index;
  int newsize;
  char *colon;
  char *buf;
  char *aContentLength;
  int iContentLength;

  // Add event data to buffer
  if (!request->body) request->body = malloc(1);
  request->body = realloc(request->body, request->bodysize + size + 1);
  memcpy(request->body + request->bodysize, data, size);
  request->bodysize += size;

  // Make string functions not segfault
  *(request->body + request->bodysize) = '\0';

  int running = 1;
  while(running) {
    switch(request->state) {
      case HTTP_PARSER_STATE_PANIC:
        return;
      case HTTP_PARSER_STATE_METHOD:

        // Wait for more data if not line break found
        index = strstr(request->body, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read method and path
        request->method  = calloc(1, 7);
        request->path    = calloc(1, 8192);
        request->version = calloc(1, 4);
        if (sscanf(request->body, "%6s %8191s HTTP/%3s", request->method, request->path, request->version) != 3) {
          request->state = HTTP_PARSER_STATE_PANIC;
          return;
        }

        // Remove the method line
        newsize = request->bodysize - 2 - (index - request->body);
        buf     = calloc(1,newsize+1);
        memcpy(buf, index + 2, newsize);
        free(request->body);
        request->body = buf;
        request->bodysize = newsize;

        // Detect query
        // No need to malloc, already done by sscanf
        index = strstr(request->path, "?");
        if (index) {
          *(index) = '\0';
          request->query = index + 1;
        }

        // Signal we're now reading headers
        request->state = HTTP_PARSER_STATE_HEADER;
        break;

      case HTTP_PARSER_STATE_HEADER:

        // Wait for more data if not line break found
        index = strstr(request->body, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Detect end of headers
        newsize = strlen(request->body);
        if (!newsize) {

          // Remove the blank line
          newsize = request->bodysize - 2;
          buf = calloc(1,newsize+1);
          memcpy(buf, index + 2, newsize);
          free(request->body);
          request->body = buf;
          request->bodysize = newsize;

          // No content-length = respond
          if (http_parser_header_get(request, "content-length")) {
            request->state = HTTP_PARSER_STATE_BODY;
          } else {
            request->state = HTTP_PARSER_STATE_RESPONSE;
          }

          break;
        }

        // Prepare new header
        header = calloc(1,sizeof(header));
        header->key = calloc(1,strlen(request->body));
        header->value = calloc(1,strlen(request->body));

        // Copy key & value
        colon = strstr(request->body, ":");
        if (colon) {
          *(colon) = '\0';
          strcpy(header->key, request->body);
          strcpy(header->value, colon + 1);
        }

        // Assign to the header list
        header->next = request->headers;
        request->headers = header;

        // Remove the header line
        newsize = request->bodysize - 2 - (index - request->body);
        buf = calloc(1,newsize+1);
        memcpy(buf, index + 2, newsize);
        free(request->body);
        request->body = buf;
        request->bodysize = newsize;

        break;

      case HTTP_PARSER_STATE_BODY:

        // Fetch the content length
        aContentLength = http_parser_header_get(request, "content-length");
        iContentLength = atoi(aContentLength);

        // Not enough data = skip
        if (request->bodysize < iContentLength) {
          running = 0;
          break;
        }

        // Change size to indicated size
        request->bodysize = iContentLength;
        request->state = HTTP_PARSER_STATE_RESPONSE;
        break;

      case HTTP_PARSER_STATE_RESPONSE:

        if (request->onRequest) {
          ev = calloc(1,sizeof(struct http_parser_event));
          ev->request = request;
          request->onRequest(ev);
          request->onRequest = NULL;
        }

        running = 0;
        break;
    }
  }
}

#ifdef __cplusplus
} // extern "C"
#endif
