#ifndef _HTTP_PARSER_H_
#define _HTTP_PARSER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_PARSER_STATE_METHOD   0
#define HTTP_PARSER_STATE_HEADER   1
#define HTTP_PARSER_STATE_BODY     2
#define HTTP_PARSER_STATE_RESPONSE 3
#define HTTP_PARSER_STATE_PANIC    666

struct http_parser_header {
  void *next;
  char *key;
  char *value;
};

struct http_parser_event {
  struct http_parser_request *request;
};

struct http_parser_request {
  int bodysize;
  int state;
  char *method;
  char *path;
  char *query;
  char *version;
  struct http_parser_header *headers;
  char *body;
  void (*onRequest)(struct http_parser_event*);
  void *udata;
};

char *http_parser_header_get(struct http_parser_request *request, char *key);
void http_parser_request_free(struct http_parser_request *request);
struct http_parser_request * http_parser_request_init();
void http_parser_request_data(struct http_parser_request *request, char *data, int size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _HTTP_PARSER_H_
