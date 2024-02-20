// vim:fdm=marker:fdl=0

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "finwo/asprintf.h"
#include "finwo/strnstr.h"
#include "tidwall/buf.h"

#include "http-parser.h"
#include "http-parser-statusses.h"

const int _HTTP_PARSER_STATE_INIT         = 0;
const int _HTTP_PARSER_STATE_HEADER       = 1;
const int _HTTP_PARSER_STATE_BODY         = 2;
const int _HTTP_PARSER_STATE_BODY_CHUNKED = 3;
const int _HTTP_PARSER_STATE_DONE         = 4;
const int _HTTP_PARSER_STATE_PANIC        = 666;

#ifndef NULL
#define NULL ((void*)0)
#endif

#if defined(_WIN32) || defined(_WIN64)
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif

// xtoi {{{
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
// }}}

// non-exported structs {{{
struct http_parser_meta {
  char *key;
  char *value;
};
// }}}

// Meta management {{{

static int fn_meta_cmp(const void *a, const void *b, void *udata) {
  const struct http_parser_meta *ta = (struct http_parser_meta *)a;
  const struct http_parser_meta *tb = (struct http_parser_meta *)b;
  return strcasecmp(ta->key, tb->key);
}

static void fn_meta_purge(void *item, void *udata) {
  struct http_parser_meta *subject = (struct http_parser_meta *)item;
  free(subject->key);
  free(subject->value);
  free(subject);
}

struct http_parser_meta * _http_parser_meta_get(struct http_parser_message *subject, const char *key) {
  struct http_parser_meta pattern = { .key = (char*)key };
  return mindex_get(subject->meta, &pattern);
}
const char * http_parser_meta_get(struct http_parser_message *subject, const char *key) {
  struct http_parser_meta *found = _http_parser_meta_get(subject, key);
  if (!found) return NULL;
  return found->value;
}

void _http_parser_meta_set(struct http_parser_message *subject, const char *key, const char *value) {
  struct http_parser_meta *meta = calloc(1, sizeof(struct http_parser_meta));
  meta->key   = strdup(key);
  meta->value = strdup(value);
  mindex_set(subject->meta, meta);
}
void http_parser_meta_set(struct http_parser_message *subject, const char *key, const char *value) {
  _http_parser_meta_set(subject, key, value);
}

void _http_parser_meta_del(struct http_parser_message *subject, const char *key) {
  struct http_parser_meta pattern = { .key = (char*)key };
  mindex_delete(subject->meta, &pattern);
}
void http_parser_meta_del(struct http_parser_message *subject, const char *key) {
  _http_parser_meta_del(subject, key);
}
// }}}

// Header management {{{

struct http_parser_meta * _http_parser_header_get(struct http_parser_message *subject, const char *key) {
  char *meta = calloc(strlen(key) + 8, sizeof(char));
  strcat(meta, "header:");
  strcat(meta, key);
  struct http_parser_meta *result = _http_parser_meta_get(subject, meta);
  free(meta);
  return result;
}

/**
 * Searches for the given key in the list of headers
 * Returns the header's value or NULL if not found
 */
const char *http_parser_header_get(struct http_parser_message *subject, const char *key) {
  struct http_parser_meta *header = _http_parser_header_get(subject, key);
  if (!header) return NULL;
  return header->value;
}

void _http_parser_header_set(struct http_parser_message *subject, const char *key, const char *value) {
  char *meta = calloc(strlen(key) + 8, sizeof(char));
  strcat(meta, "header:");
  strcat(meta, key);
  _http_parser_meta_set(subject, meta, value);
  free(meta);
}

/**
 * Write a header into the subject's list of headers
 */
void http_parser_header_set(struct http_parser_message *subject, const char *key, const char *value) {
  return _http_parser_header_set(subject, key, value);
}

void _http_parser_header_del(struct http_parser_message *subject, const char *key) {
  char *meta = calloc(strlen(key) + 8, sizeof(char));
  strcat(meta, "header:");
  strcat(meta, key);
  _http_parser_meta_del(subject, meta);
  free(meta);
}

void http_parser_header_del(struct http_parser_message *subject, const char *key) {
  _http_parser_header_del(subject, key);
}

// }}}

/**
 * Frees everything in a http_message that was malloc'd by http-parser
 */
void http_parser_message_free(struct http_parser_message *subject) {
  if (subject->method ) free(subject->method);
  if (subject->path   ) free(subject->path);
  if (subject->version) free(subject->version);
  if (subject->body   ) { buf_clear(subject->body); free(subject->body); }
  if (subject->meta   ) mindex_free(subject->meta);
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
  message->meta      = mindex_init(
      fn_meta_cmp,
      fn_meta_purge,
      NULL
  );
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

  // Detect colon
  index = strstr(message->body->data, ": ");
  if (!index) {
    http_parser_message_remove_body_string(message);
    return 1;
  }

  // Split by the found colon & skip leading whitespace
  *(index) = '\0';
  index++;
  while(*(index) == ' ') index++;

  // Insert the header in our map
  _http_parser_header_set(message, message->body->data, index);

  // Remove the header remainder
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

  // Headers
  int i;
  struct http_parser_meta *meta;
  if (response->meta) {
    for(i=0; i<mindex_length(response->meta); i++) {
      meta = mindex_nth(response->meta, i);
      if (strncmp("header:", meta->key, 7)) continue;
      strcat(result->data, (meta->key + 7));
      strcat(result->data, ": ");
      strcat(result->data, meta->value);
      strcat(result->data, "\r\n");
    }
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
  int i;
  struct http_parser_meta *meta;
  if (request->meta) {
    for(i=0; i<mindex_length(request->meta); i++) {
      meta = mindex_nth(request->meta, i);
      if (strncmp("header:", meta->key, 7)) continue;
      strcat(result->data, (meta->key + 7));
      strcat(result->data, ": ");
      strcat(result->data, meta->value);
      strcat(result->data, "\r\n");
    }
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
 * Triggers onResponse if set
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
      case _HTTP_PARSER_STATE_PANIC:
        return;
      case _HTTP_PARSER_STATE_INIT:

        // Wait for more data if not line break found
        index = strstr(request->body->data, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read method and path
        request->method  = calloc(1, 16);
        request->path    = calloc(1, 8192);
        request->version = calloc(1, 4);
        if (sscanf(request->body->data, "%15s %8191s HTTP/%3s", request->method, request->path, request->version) != 3) {
          request->_state = _HTTP_PARSER_STATE_PANIC;
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
        request->_state = _HTTP_PARSER_STATE_HEADER;
        break;

      case _HTTP_PARSER_STATE_HEADER:
        if (!http_parser_message_read_header(request)) {
          if (
              http_parser_header_get(request, "content-length") ||
              http_parser_header_get(request, "transfer-encoding")
          ) {
            request->_state = _HTTP_PARSER_STATE_BODY;
          } else {
            request->_state = _HTTP_PARSER_STATE_DONE;
          }
        }
        break;

      case _HTTP_PARSER_STATE_BODY:

        // Detect chunked encoding
        if (request->chunksize == -1) {
          aChunkSize = http_parser_header_get(request, "transfer-encoding");
          if (aChunkSize) {
            if (!strcasecmp(aChunkSize, "chunked")) {
              request->_state = _HTTP_PARSER_STATE_BODY_CHUNKED;
              break;
            }
          }
        }

        // Fetch the content length
        aContentLength = http_parser_header_get(request, "content-length");
        if (!aContentLength) {
          request->_state = _HTTP_PARSER_STATE_DONE;
          break;
        }
        iContentLength = atoi(aContentLength);

        // Not enough data = skip
        if (request->body->len < iContentLength) {
          return;
        }

        // Change size to indicated size
        request->_state = _HTTP_PARSER_STATE_DONE;
        break;

      case _HTTP_PARSER_STATE_BODY_CHUNKED:
        res = http_parser_message_read_chunked(request);

        if (res == 0) {
          // Done
          request->_state = _HTTP_PARSER_STATE_DONE;
        } else if (res == 1) {
          // More data needed
          return;
        } else if (res == 2) {
          // Still reading
        }

        break;

      case _HTTP_PARSER_STATE_DONE:

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
      case _HTTP_PARSER_STATE_PANIC:
        return;
      case _HTTP_PARSER_STATE_INIT:
        // Wait for more data if not line break found
        index = strstr(response->body->data, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read version and status
        response->version       = calloc(1, 8);
        response->statusMessage = calloc(1, 64);
        aStatus                 = calloc(1, 8);
        if (sscanf(response->body->data, "HTTP/%7s %7s %63[^\r\n]", response->version, aStatus, response->statusMessage) != 3) {
          response->_state = _HTTP_PARSER_STATE_PANIC;
          return;
        }

        // Turn the text status into a number
        response->status = atoi(aStatus);
        free(aStatus);

        // Remove status line
        http_parser_message_remove_body_string(response);

        // Signal we're now reading headers
        response->_state = _HTTP_PARSER_STATE_HEADER;
        break;

      case _HTTP_PARSER_STATE_HEADER:
        if (!http_parser_message_read_header(response)) {
          if (
              http_parser_header_get(response, "content-length") ||
              http_parser_header_get(response, "transfer-encoding")
          ) {
            response->_state = _HTTP_PARSER_STATE_BODY;
          } else {
            response->_state = _HTTP_PARSER_STATE_DONE;
          }
        }
        break;

      case _HTTP_PARSER_STATE_BODY:
        // Detect chunked encoding
        if (response->chunksize == -1) {
          aChunkSize = http_parser_header_get(response, "transfer-encoding");
          if (aChunkSize) {
            if (!strcasecmp(aChunkSize, "chunked")) {
              response->_state = _HTTP_PARSER_STATE_BODY_CHUNKED;
              break;
            }
          }
        }

        // Fetch the content length
        aContentLength = http_parser_header_get(response, "content-length");
        if (!aContentLength) {
          response->_state = _HTTP_PARSER_STATE_DONE;
          break;
        }
        iContentLength = atoi(aContentLength);

        // Not enough data = skip
        if (response->body->len < iContentLength) {
          return;
        }

        // Change size to indicated size
        response->_state = _HTTP_PARSER_STATE_DONE;
        break;

      case _HTTP_PARSER_STATE_BODY_CHUNKED:
        res = http_parser_message_read_chunked(response);

        if (res == 0) {
          // Done
          response->_state = _HTTP_PARSER_STATE_DONE;
        } else if (res == 1) {
          // More data needed
          return;
        } else if (res == 2) {
          // Still reading
        }

        break;

      case _HTTP_PARSER_STATE_DONE:
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
