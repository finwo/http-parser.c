#ifndef _HTTP_PARSER_H_
#define _HTTP_PARSER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_PARSER_STATE_METHOD       0
#define HTTP_PARSER_STATE_HEADER       1
#define HTTP_PARSER_STATE_BODY         2
#define HTTP_PARSER_STATE_BODY_CHUNKED 3
#define HTTP_PARSER_STATE_DONE         4
#define HTTP_PARSER_STATE_PANIC    666

struct http_parser_header {
  void *next;
  char *key;
  char *value;
};

struct http_parser_event {
  struct http_parser_message *request;
  struct http_parser_message *response;
  void *udata;
};

struct http_parser_message {
  int ready;
  int status;
  char *method;
  char *path;
  char *query;
  char *version;
  struct http_parser_header *headers;
  char *body;
  char *buf;
  int bufsize;
  int chunksize;
  int bodysize;
  int _state;
};

struct http_parser_pair {
  struct http_parser_message *request;
  struct http_parser_message *response;
  void *udata;
  void (*onRequest)(struct http_parser_event*);
  void (*onResponse)(struct http_parser_event*);
};

// Header management
char *http_parser_header_get(struct http_parser_message *subject, char *key);
void http_parser_header_set(struct http_parser_message *subject, char *key, char *value);
char *http_parser_header_del(struct http_parser_message *subject, char *key);

struct http_parser_pair    * http_parser_pair_init(void *udata);
struct http_parser_message * http_parser_request_init();
struct http_parser_message * http_parser_response_init();

void http_parser_request_data(struct http_parser_message *request, char *data, int size);

void http_parser_response_data(struct http_parser_message *request, char *data, int size);

void http_parser_pair_request_data(struct http_parser_pair *pair, char *data, int size);
void http_parser_pair_response_data(struct http_parser_pair *pair, char *data, int size);

void http_parser_pair_free(struct http_parser_pair *pair);
void http_parser_message_free(struct http_parser_message *subject);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _HTTP_PARSER_H_
