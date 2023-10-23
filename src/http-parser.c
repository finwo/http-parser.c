#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "finwo/asprintf.h"
#include "finwo/strnstr.h"
#include "tidwall/buf.h"

#include "http-parser.h"
#include "http-parser-statusses.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/**
 * Convert hexidecimal string to int
 */
int xtoi(char *str) {
  char *p = str;
  int i = 0;
  int sign = 1;
  while(*p) {
    if ( *p == '-' ) sign = -1;

    if ( *p >= '0' && *p <= '9' ) {
      i *= 16;
      i += (*p) - '0';
    }

    if ( *p >= 'a' && *p <= 'f' ) {
      i *= 16;
      i += 10 + (*p) - 'a';
    }

    if ( *p >= 'A' && *p <= 'F' ) {
      i *= 16;
      i += 10 + (*p) - 'A';
    }

    p = p+1;
  }

  return sign * i;
}

/**
 * Frees everything in a header that was malloc'd by http-parser
 */
void http_parser_header_free(struct http_parser_header *header) {
  if (header->next) http_parser_header_free(header->next);
  if (header->key) free(header->key);
  if (header->value) free(header->value);
  free(header);
}

/**
 * Internal, returns the pointer to a header entry that matches the key
 */
struct http_parser_header * _http_parser_header_get(struct http_parser_message *subject, const char *key) {
  struct http_parser_header *header = subject->headers;
  while(header) {
    if (!strcasecmp(header->key, key)) {
      return header;
    }
    header = header->next;
  }
  return NULL;
}

/**
 * Searches for the given key in the list of headers
 * Returns the header's value or NULL if not found
 */
const char *http_parser_header_get(struct http_parser_message *subject, const char *key) {
  struct http_parser_header *header = _http_parser_header_get(subject, key);
  if (!header) return NULL;
  char *value = header->value;
  while(*value == ' ') value++;
  return value;
}

/**
 * Write a header into the subject's list of headers
 */
void http_parser_header_set(struct http_parser_message *subject, const char *key, const char *value) {
  struct http_parser_header *header = _http_parser_header_get(subject, key);
  if (header) {
    free(header->value);
    header->value = strdup(value);
  } else {
    http_parser_header_add(subject, key, value);
  }
}

/**
 * Add a header into the subject's list of headers
 */
void http_parser_header_add(struct http_parser_message *subject, const char *key, const char *value) {
  struct http_parser_header *header = malloc(sizeof(struct http_parser_header));
  header->key      = strdup(key);
  header->value    = strdup(value);
  header->next     = subject->headers;
  subject->headers = header;
}

/**
 * Write a header into the subject's list of headers
 */
void http_parser_header_del(struct http_parser_message *subject, const char *key) {
  struct http_parser_header *header_prev = NULL;
  struct http_parser_header *header_cur  = subject->headers;
  while(header_cur) {
    if (strcasecmp(header_cur->key, key) == 0) {
      if (header_prev) {
        header_prev->next = header_cur->next;
      } else {
        subject->headers = header_cur->next;
      }
      header_cur->next = NULL;
      http_parser_header_free(header_cur);
      if (header_prev) {
        header_cur = header_prev->next;
      } else {
        header_cur = subject->headers;
      }
      continue;
    }
    header_prev = header_cur;
    header_cur  = header_cur->next;
  }
}

/**
 * Frees everything in a http_message that was malloc'd by http-parser
 */
void http_parser_message_free(struct http_parser_message *subject) {
  if (subject->method ) free(subject->method);
  if (subject->path   ) free(subject->path);
  if (subject->version) free(subject->version);
  if (subject->body   ) { buf_clear(subject->body); free(subject->body); }
  if (subject->headers) http_parser_header_free(subject->headers);
  if (subject->buf    ) free(subject->buf);
  free(subject);
}

/**
 * Frees everything in a http pair that was malloc'd by http-parser
 */
void http_parser_pair_free(struct http_parser_pair *pair) {
  if (pair->request) http_parser_message_free(pair->request);
  if (pair->response) http_parser_message_free(pair->response);
  free(pair);
}

/**
 * Initializes a http_message as request
 */
struct http_parser_message * http_parser_request_init() {
  struct http_parser_message *message = calloc(1, sizeof(struct http_parser_message));
  message->chunksize = -1;
  return message;
}

/**
 * Initializes a http_message as reponse
 */
struct http_parser_message * http_parser_response_init() {
  struct http_parser_message *message = http_parser_request_init();
  message->status = 200;
  message->version = calloc(1,4);
  strcpy(message->version, "1.1");
  return message;
}

/**
 * Initialize a http_pair with userdata
 */
struct http_parser_pair * http_parser_pair_init(void *udata) {
  struct http_parser_pair *pair = calloc(1, sizeof(struct http_parser_pair));
  pair->request  = http_parser_request_init();
  pair->response = http_parser_response_init();
  pair->udata    = udata;
  return pair;
}


/**
 * Removed N bytes from the beginning of the message body
 */
static void http_parser_message_remove_body_bytes(struct http_parser_message *message, int bytes) {
  struct buf *newbuf = calloc(1, sizeof(struct buf));
  int size = message->body->len - bytes;

  if (size > 0) {
    buf_append(newbuf, message->body->data + bytes, size);
  }

  if (message->body) {
    buf_clear(message->body);
    free(message->body);
  }
  message->body = newbuf;
}

/**
 * Removes the first string from a http_message's body
 */
static void http_parser_message_remove_body_string(struct http_parser_message *message) {
  int length = strlen(message->body->data);
  http_parser_message_remove_body_bytes(message, length + 2);
}

/**
 * Reads a header from a message's body and removes that lines from the body
 *
 * Caution: does not support multi-line headers yet
 */
static int http_parser_message_read_header(struct http_parser_message *message) {
  struct http_parser_header *header;
  char *index;

  // Require more data if no line break found
  index = strnstr(message->body->data, "\r\n", message->body->len);
  if (!index) return 1;
  *(index) = '\0';

  // Detect end of headers
  if (!strlen(message->body->data)) { // Using strlen, due to possible \r\n replacement
    http_parser_message_remove_body_string(message);
    return 0;
  }

  // Prepare new header
  header        = calloc(1,sizeof(struct http_parser_message));
  header->key   = calloc(1,strlen(message->body->data)); // Using strlen, due to possible \r\n replacement
  header->value = calloc(1,strlen(message->body->data)); // Using strlen, due to possible \r\n replacement

  // Detect colon
  index = strstr(message->body->data, ": ");
  if (!index) {
    http_parser_header_free(header);
    http_parser_message_remove_body_string(message);
    return 1;
  }

  // Copy key & value
  *(index) = '\0';
  strcpy(header->key, message->body->data);
  strcpy(header->value, index + 2);

  // Assign to the header list
  header->next     = message->headers;
  message->headers = header;

  // Remove the header line
  // Twice, because we split the string
  http_parser_message_remove_body_string(message);
  http_parser_message_remove_body_string(message);
  return 2;
}

/**
 * Reads chunked data
 */
static int http_parser_message_read_chunked(struct http_parser_message *message) {
  char *aChunkSize;
  char *index;
  struct http_parser_event *ev;

  // Attempt reading the chunk size
  if (message->chunksize == -1) {

    // Check if we have a line
    index = strnstr(message->body->data, "\r\n", message->body->len);
    if (!index) {
      return 1;
    }
    *(index) = '\0';

    // Empty line = skip
    if (!strlen(message->body->data)) {
      http_parser_message_remove_body_string(message);
      return 2;
    }

    // Read hex chunksize
    aChunkSize = calloc(1, 17);
    sscanf(message->body->data, "%16s", aChunkSize);
    message->chunksize = xtoi(aChunkSize);
    free(aChunkSize);

    // Remove chunksize line
    http_parser_message_remove_body_string(message);

    // 0 = EOF
    if (message->chunksize == 0) {
      return 0;
    }

    // Signal we're still reading
    return 2;
  }

  // Create buffer if not present yet
  if (!message->buf) {
    message->buf = calloc(1,sizeof(struct buf));
  }

  // Ensure the body has enough data
  if (message->body->len < message->chunksize) {
    return 1;
  }

  // Either call onChunk method OR copy into message buffer
  if (message->onChunk) {
    // Call onChunk if the message has that set
    ev            = calloc(1,sizeof(struct http_parser_event));
    ev->udata     = message->udata;
    ev->chunk     = &((struct buf){
      .len  = message->chunksize,
      .cap  = message->chunksize,
      .data = message->body->data,
    });
    message->onChunk(ev);
    free(ev);
  } else {
    buf_append(message->buf, message->body->data, message->chunksize);
  }

  // Remove chunk from receiving data and reset chunking
  http_parser_message_remove_body_bytes(message, message->chunksize);
  message->chunksize = -1;

  // No error or end encountered
  return 2;
}

const char * http_parser_status_message(int status) {
  int i;
  for(i=0; http_parser_statusses[i].status; i++) {
    if (http_parser_statusses[i].status == status) {
      return http_parser_statusses[i].message;
    }
  }
  return NULL;
}

struct buf * http_parser_sprint_pair_response(struct http_parser_pair *pair) {
  return http_parser_sprint_response(pair->response);
}

struct buf * http_parser_sprint_pair_request(struct http_parser_pair *pair) {
  return http_parser_sprint_request(pair->request);
}

void _http_parser_sprint_headers(char *target, struct http_parser_header *header) {
  if (header->next) _http_parser_sprint_headers(target, header->next);
  strcat(target, header->key);
  strcat(target, ": ");
  strcat(target, header->value);
  strcat(target, "\r\n");
}

// Caution: headers should fit in 64k
struct buf * http_parser_sprint_response(struct http_parser_message *response) {
  struct buf *result = calloc(1, sizeof(struct buf));
  result->cap  = 65536;
  result->data = calloc(1, result->cap);

  // Status
  sprintf(result->data,
      "HTTP/%s %d %s\r\n"
      , response->version
      , response->status
      , response->statusMessage ? response->statusMessage : http_parser_status_message(response->status)
  );

  if (response->headers) {
    _http_parser_sprint_headers(result->data, response->headers);
  }

  strcat(result->data, "\r\n");

  // Treat result as buffer from here
  result->len = strlen(result->data);

  if (response->body) {
    buf_append(result, response->body->data, response->body->len);
  }

  return result;
}

// Caution: headers should fit in 64k
struct buf * http_parser_sprint_request(struct http_parser_message *request) {
  struct buf *result = calloc(1, sizeof(struct buf));
  result->cap  = 65536;
  result->data = calloc(1, result->cap);

  char *tmppath;
  char *path = request->path;
  int isPathAllocced = 0;
  if (!path) path = "/";
  if (strstr(path, "/") != path) {
    tmppath        = path;
    path           = NULL;
    isPathAllocced = true;
    asprintf(&path, "/%s", tmppath);
  }

  // Status
  sprintf(result->data,
      "%s %s"
      , request->method
      , path
  );

  // Query
  if (request->query) {
    strcat(result->data, "?");
    strcat(result->data, request->query);
  }

  // HTTP version
  strcat(result->data, " HTTP/");
  strcat(result->data, request->version);
  strcat(result->data, "\r\n");

  // Headers
  if (request->headers) {
    _http_parser_sprint_headers(result->data, request->headers);
  }

  strcat(result->data, "\r\n");

  // Handle chunked header
  const char *aTransferEncoding = http_parser_header_get(request, "transfer-encoding");
  int isChunked = 0;
  if (aTransferEncoding && strcasecmp(aTransferEncoding, "chunked")) {
    isChunked = 1;
    sprintf(result->data + strlen(result->data), "%lx\r\n", request->body->len);
  }

  // Treat result as buffer from here
  result->len = strlen(result->data);

  if (request->body) {
    buf_append(result, request->body->data, request->body->len);
  }

  if (isChunked) {
    buf_append(result, "0\r\n\r\n", 5);
  }

  if (isPathAllocced) {
    free(path);
  }

  return result;
}

/**
 * Pass data into the pair's request
 *
 * Triggers onRequest if set
 */
void http_parser_pair_request_data(struct http_parser_pair *pair, const struct buf *data) {
  struct http_parser_event *ev;
  http_parser_request_data(pair->request, data);
  if (pair->request->ready && pair->onRequest) {
    ev           = calloc(1,sizeof(struct http_parser_event));
    ev->request  = pair->request;
    ev->response = pair->response;
    ev->pair     = pair;
    ev->udata    = pair->udata;
    pair->onRequest(ev);
    free(ev);
    pair->onRequest = NULL;
  }
}

/**
 * Pass data into the pair's request
 *
 * Triggers onRequest if set
 */
void http_parser_pair_response_data(struct http_parser_pair *pair, const struct buf *data) {
  struct http_parser_event *ev;
  http_parser_response_data(pair->response, data);
  if (pair->response->ready && pair->onResponse) {
    ev           = calloc(1,sizeof(struct http_parser_event));
    ev->request  = pair->request;
    ev->response = pair->response;
    ev->pair     = pair;
    ev->udata    = pair->udata;
    pair->onResponse(ev);
    free(ev);
    pair->onResponse = NULL;
  }
}

/**
 * Insert data into a http_message, acting as if it's a request
 */
void http_parser_request_data(struct http_parser_message *request, const struct buf *data) {
  char *index;
  const char *aContentLength;
  int iContentLength;
  const char *aChunkSize;
  int res;

  // Add event data to buffer
  if (!request->body) request->body = calloc(1, sizeof(struct buf));
  buf_append(request->body, data->data, data->len);

  while(1) {
    switch(request->_state) {
      case HTTP_PARSER_STATE_PANIC:
        return;
      case HTTP_PARSER_STATE_INIT:

        // Wait for more data if not line break found
        index = strstr(request->body->data, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read method and path
        request->method  = calloc(1, 16);
        request->path    = calloc(1, 8192);
        request->version = calloc(1, 4);
        if (sscanf(request->body->data, "%15s %8191s HTTP/%3s", request->method, request->path, request->version) != 3) {
          request->_state = HTTP_PARSER_STATE_PANIC;
          return;
        }

        // Remove method line
        http_parser_message_remove_body_string(request);

        // Detect query
        // No need to malloc, already done by sscanf
        index = strstr(request->path, "?");
        if (index) {
          *(index) = '\0';
          request->query = index + 1;
        }

        // Signal we're now reading headers
        request->_state = HTTP_PARSER_STATE_HEADER;
        break;

      case HTTP_PARSER_STATE_HEADER:
        if (!http_parser_message_read_header(request)) {
          if (
              http_parser_header_get(request, "content-length") ||
              http_parser_header_get(request, "transfer-encoding")
          ) {
            request->_state = HTTP_PARSER_STATE_BODY;
          } else {
            request->_state = HTTP_PARSER_STATE_DONE;
          }
        }
        break;

      case HTTP_PARSER_STATE_BODY:

        // Detect chunked encoding
        if (request->chunksize == -1) {
          aChunkSize = http_parser_header_get(request, "transfer-encoding");
          if (aChunkSize) {
            if (!strcasecmp(aChunkSize, "chunked")) {
              request->_state = HTTP_PARSER_STATE_BODY_CHUNKED;
              break;
            }
          }
        }

        // Fetch the content length
        aContentLength = http_parser_header_get(request, "content-length");
        if (!aContentLength) {
          request->_state = HTTP_PARSER_STATE_DONE;
          break;
        }
        iContentLength = atoi(aContentLength);

        // Not enough data = skip
        if (request->body->len < iContentLength) {
          return;
        }

        // Change size to indicated size
        request->_state = HTTP_PARSER_STATE_DONE;
        break;

      case HTTP_PARSER_STATE_BODY_CHUNKED:
        res = http_parser_message_read_chunked(request);

        if (res == 0) {
          // Done
          request->_state = HTTP_PARSER_STATE_DONE;
        } else if (res == 1) {
          // More data needed
          return;
        } else if (res == 2) {
          // Still reading
        }

        break;

      case HTTP_PARSER_STATE_DONE:

        // Temporary buffer > direct buffer
        if (request->buf) {
          if (request->body) {
            buf_clear(request->body);
            free(request->body);
          }
          request->body = request->buf;
          request->buf  = NULL;
        }

        // Mark the request as ready
        request->ready  = 1;
        return;
    }
  }
}

/**
 * Insert data into a http_message, acting as if it's a request
 */
void http_parser_response_data(struct http_parser_message *response, const struct buf *data) {
  char *index;
  char *aStatus;
  int iContentLength;
  const char *aContentLength;
  const char *aChunkSize;
  int res;

  // Add event data to buffer
  if (!response->body) response->body = calloc(1, sizeof(struct buf));
  buf_append(response->body, data->data, data->len);

  while(1) {
    switch(response->_state) {
      case HTTP_PARSER_STATE_PANIC:
        return;
      case HTTP_PARSER_STATE_INIT:

        // Wait for more data if not line break found
        index = strstr(response->body->data, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read version and status
        response->version       = calloc(1, 4);
        response->statusMessage = calloc(1, 8192);
        aStatus                 = calloc(1, 4);
        if (sscanf(response->body->data, "HTTP/%3s %3s %8191c", response->version, aStatus, response->statusMessage) != 3) {
          response->_state = HTTP_PARSER_STATE_PANIC;
          return;
        }

        // Turn the text status into a number
        response->status = atoi(aStatus);
        free(aStatus);

        // Remove status line
        http_parser_message_remove_body_string(response);

        // Signal we're now reading headers
        response->_state = HTTP_PARSER_STATE_HEADER;
        break;

      case HTTP_PARSER_STATE_HEADER:
        if (!http_parser_message_read_header(response)) {
          if (
              http_parser_header_get(response, "content-length") ||
              http_parser_header_get(response, "transfer-encoding")
          ) {
            response->_state = HTTP_PARSER_STATE_BODY;
          } else {
            response->_state = HTTP_PARSER_STATE_DONE;
          }
        }
        break;

      case HTTP_PARSER_STATE_BODY:

        // Detect chunked encoding
        if (response->chunksize == -1) {
          aChunkSize = http_parser_header_get(response, "transfer-encoding");
          if (aChunkSize) {
            if (!strcasecmp(aChunkSize, "chunked")) {
              response->_state = HTTP_PARSER_STATE_BODY_CHUNKED;
              break;
            }
          }
        }

        // Fetch the content length
        aContentLength = http_parser_header_get(response, "content-length");
        if (!aContentLength) {
          response->_state = HTTP_PARSER_STATE_DONE;
          break;
        }
        iContentLength = atoi(aContentLength);

        // Not enough data = skip
        if (response->body->len < iContentLength) {
          return;
        }

        // Change size to indicated size
        response->_state = HTTP_PARSER_STATE_DONE;
        break;

      case HTTP_PARSER_STATE_BODY_CHUNKED:
        res = http_parser_message_read_chunked(response);

        if (res == 0) {
          // Done
          response->_state = HTTP_PARSER_STATE_DONE;
        } else if (res == 1) {
          // More data needed
          return;
        } else if (res == 2) {
          // Still reading
        }

        break;

      case HTTP_PARSER_STATE_DONE:

        // Temporary buffer > direct buffer
        if (response->buf) {
          if (response->body) {
            buf_clear(response->body);
            free(response->body);
          }
          response->body = response->buf;
          response->buf  = NULL;
        }

        // Mark the request as ready
        response->ready = 1;
        return;
    }
  }
}

#ifdef __cplusplus
} // extern "C"
#endif
