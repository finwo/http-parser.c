#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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
 * Searches for the given key in the list of headers
 * Returns the header's value or NULL if not found
 */
char *http_parser_header_get(struct http_parser_message *subject, char *key) {
  struct http_parser_header *header = subject->headers;
  char *value;
  while(header) {
    if (!strcasecmp(key, header->key)) {
      value = header->value;
      while(*(value) == ' ') {
        value++;
      }
      return value;
    }
    header = header->next;
  }
  return NULL;
}

/**
 * Write a header into the subject's list of headers
 */
void http_parser_header_set(struct http_parser_message *subject, char *key, char *value) {
  struct http_parser_header *header = malloc(sizeof(struct http_parser_header));
  header->key = key;
  header->value = value;
  header->next = subject->headers;
  subject->headers = header;
}

/**
 * Frees everything in a http_message that was malloc'd by http-parser
 */
void http_parser_message_free(struct http_parser_message *subject) {
  if (subject->method) free(subject->method);
  if (subject->path) free(subject->path);
  if (subject->version) free(subject->version);
  if (subject->body) free(subject->body);
  if (subject->headers) http_parser_header_free(subject->headers);
  if (subject->buf) free(subject->buf);
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
 * Removed N byte from the beginning of the message body
 */
static void http_parser_message_remove_body_bytes(struct http_parser_message *message, int bytes) {
  int size = message->bodysize - bytes;
  char *buf = calloc(1, size + 1);
  memcpy(buf, message->body + bytes, size);
  free(message->body);
  message->body = buf;
  message->bodysize = size;
}

/**
 * Removes the first string from a http_message's body
 */
static void http_parser_message_remove_body_string(struct http_parser_message *message) {
  int length = strlen(message->body);
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
  index = strstr(message->body, "\r\n");
  if (!index) return 1;
  *(index) = '\0';

  // Detect end of headers
  if (!strlen(message->body)) {
    http_parser_message_remove_body_string(message);
    return 0;
  }

  // Prepare new header
  header = calloc(1,sizeof(header));
  header->key = calloc(1,strlen(message->body));
  header->value = calloc(1,strlen(message->body));

  // Detect colon
  index = strstr(message->body, ":");
  if (!index) {
    http_parser_header_free(header);
    http_parser_message_remove_body_string(message);
    return 1;
  }

  // Copy key & value
  *(index) = '\0';
  strcpy(header->key, message->body);
  strcpy(header->value, index + 1);

  // Assign to the header list
  header->next = message->headers;
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

  // Attempt reading the chunk size
  if (message->chunksize == -1) {

    // Check if we have a line
    index = strstr(message->body, "\r\n");
    if (!index) {
      return 1;
    }
    *(index) = '\0';

    // Empty line = skip
    if (!strlen(message->body)) {
      http_parser_message_remove_body_string(message);
      return 2;
    }

    // Read hex chunksize
    aChunkSize = calloc(1, 17);
    sscanf(message->body, "%16s", aChunkSize);
    message->chunksize = xtoi(aChunkSize);
    free(aChunkSize);

    // Add it to the total
    message->chunktotal += message->chunksize;

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
    message->buf = calloc(1,1);
    message->bufsize = 0;
  }

  // Ensure the body has enough data
  if (message->bodysize < message->chunksize) {
    return 1;
  }

  // Copy data into buffer
  message->buf = realloc(message->buf, message->bufsize + message->chunksize + 1);
  memcpy(message->buf + message->bufsize, message->body, message->chunksize );
  message->bufsize += message->chunksize;
  *(message->buf + message->bufsize) = '\0';

  // Remove chunk from receiving data and reset chunking
  http_parser_message_remove_body_bytes(message, message->chunksize);
  message->chunksize = -1;

  // No error or end encountered
  return 2;
}

char * http_parser_sprint_pair_response(struct http_parser_pair *pair) {
  return http_parser_sprint_response(pair->response);
}

char * http_parser_sprint_pair_quest(struct http_parser_pair *pair) {
  return http_parser_sprint_response(pair->response);
}

char * http_parser_sprint_response(struct http_parser_message *response) {
  char *result = calloc(1,256);
  struct http_parser_header *header;
  int index, length;

  // Status
  sprintf(result,
      "HTTP/%s %d %s\r\n"
      , response->version
      , response->status
      , response->statusMessage
  );

  // Headers
  header = response->headers;
  while(header) {
    index = strlen(result);
    length = index +
      strlen(header->key) +
      2 +
      strlen(header->value) +
      3 +
      3;

    // Asign memory
    result = realloc(result,length);
    *(result + length) = '\0';

    // Write header
    sprintf(result + index,
      "%s: %s\r\n",
      header->key,
      header->value
    );

    // Next header
    header = header->next;
  }

  strcpy( result + strlen(result), "\r\n" );

  // Write body
  index = strlen(result);
  length = index + response->bodysize;
  result = realloc(result, length + 1);
  *(result + length) = '\0';
  memcpy( result + index + 2, response->body, response->bodysize );

  return result;
}

char * http_parser_sprint_request(struct http_parser_message *request) {
  char *result = calloc(1,8192);
  struct http_parser_header *header;
  int index, length;

  // Status
  sprintf(result,
      "%s %s"
      , request->method
      , request->path
  );

  // Query
  if (request->query) {
    *(result + strlen(result)) = '?';
    strcpy( result + strlen(result), request->query );
  }

  // HTTP version
  strcpy( result + strlen(result), " HTTP/" );
  strcpy( result + strlen(result), request->version );
  strcpy( result + strlen(result), "\r\n" );

  // Headers
  header = request->headers;
  while(header) {
    index = strlen(result);
    length = index +
      strlen(header->key) +
      2 +
      strlen(header->value) +
      3;

    // Asign memory
    result = realloc(result,length);
    *(result + length) = '\0';

    // Write header
    sprintf(result + index,
      "%s: %s\r\n",
      header->key,
      header->value
    );

    // Next header
    header = header->next;
  }

  strcpy( result + strlen(result), "\r\n" );

  // Handle chunked header
  if (http_parser_header_get(request, "transfer-encoding")) {
    result = realloc(result, strlen(result) + 20);
    sprintf(result+strlen(result), "%x\r\n", request->bodysize);
  }

  // Write body
  index = strlen(result);
  length = index + request->bodysize;
  result = realloc(result, length + 1);
  *(result + length) = '\0';
  memcpy( result + index, request->body, request->bodysize );

  if (http_parser_header_get(request, "transfer-encoding")) {
    result = realloc(result, length + 6);
    *(result+length+0) = '0';
    *(result+length+1) = '\r';
    *(result+length+2) = '\n';
    *(result+length+3) = '\r';
    *(result+length+4) = '\n';
    *(result+length+5) = '\0';
  }

  return result;
}

/**
 * Pass data into the pair's request
 *
 * Triggers onRequest if set
 */
void http_parser_pair_request_data(struct http_parser_pair *pair, char *data, int size) {
  struct http_parser_event *ev;
  http_parser_request_data(pair->request, data, size);
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
void http_parser_pair_response_data(struct http_parser_pair *pair, char *data, int size) {
  struct http_parser_event *ev;
  http_parser_response_data(pair->request, data, size);
  if (pair->request->ready && pair->onResponse) {
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
void http_parser_request_data(struct http_parser_message *request, char *data, int size) {
  char *index;
  char *aContentLength;
  int iContentLength;
  char *aChunkSize;
  int res;

  // Add event data to buffer
  if (!request->body) request->body = malloc(1);
  request->body = realloc(request->body, request->bodysize + size + 1);
  memcpy(request->body + request->bodysize, data, size);
  request->bodysize += size;

  // Make string functions not segfault
  *(request->body + request->bodysize) = '\0';

  while(1) {
    switch(request->_state) {
      case HTTP_PARSER_STATE_PANIC:
        return;
      case HTTP_PARSER_STATE_INIT:

        // Wait for more data if not line break found
        index = strstr(request->body, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read method and path
        request->method  = calloc(1, 7);
        request->path    = calloc(1, 8192);
        request->version = calloc(1, 4);
        if (sscanf(request->body, "%6s %8191s HTTP/%3s", request->method, request->path, request->version) != 3) {
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
            if (!strcmp(aChunkSize, "chunked")) {
              request->chunktotal = 0;
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
        if (request->bodysize < iContentLength) {
          return;
        }

        // Change size to indicated size
        request->bodysize = iContentLength;
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
          free(request->body);
          request->body = request->buf;
          request->buf  = NULL;
          request->bodysize = request->chunktotal;
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
void http_parser_response_data(struct http_parser_message *response, char *data, int size) {
  char *index;
  char *aStatus;
  int iContentLength;
  char *aContentLength;
  char *aChunkSize;
  int res;

  // Add event data to buffer
  if (!response->body) response->body = malloc(1);
  response->body = realloc(response->body, response->bodysize + size + 1);
  memcpy(response->body + response->bodysize, data, size);
  response->bodysize += size;

  // Make string functions not segfault
  *(response->body + response->bodysize) = '\0';

  while(1) {
    switch(response->_state) {
      case HTTP_PARSER_STATE_PANIC:
        return;
      case HTTP_PARSER_STATE_INIT:

        // Wait for more data if not line break found
        index = strstr(response->body, "\r\n");
        if (!index) return;
        *(index) = '\0';

        // Read version and status
        response->version       = calloc(1, 4);
        response->statusMessage = calloc(1, 8192);
        aStatus                 = calloc(1, 4);
        if (sscanf(response->body, "HTTP/%3s %3s %8191c", response->version, aStatus, response->statusMessage) != 3) {
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
            if (!strcmp(aChunkSize, "chunked")) {
              response->chunktotal = 0;
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
        if (response->bodysize < iContentLength) {
          return;
        }

        // Change size to indicated size
        response->bodysize = iContentLength;
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
          free(response->body);
          response->body = response->buf;
          response->buf  = NULL;
          response->bodysize = response->chunktotal;
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
