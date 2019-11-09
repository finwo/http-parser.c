#ifndef CROSYNC_HTTP_H
#define CROSYNC_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_STATE_METHOD   0
#define HTTP_STATE_HEADER   1
#define HTTP_STATE_BODY     2
#define HTTP_STATE_RESPONSE 3
#define HTTP_STATE_PANIC    666

struct http_header {
  void *next;
  char *key;
  char *value;
};

struct http_event {
  struct http_request *request;
};

struct http_request {
  int bodysize;
  int state;
  char *method;
  char *path;
  struct http_header *headers;
  char *body;
  void (*onRequest)(struct http_event*);
  void *udata;
};

char *http_header_get(struct http_request *request, char *key);
void http_request_free(struct http_request *request);
struct http_request * http_request_init();
void http_request_data(struct http_request *request, char *data, int size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CROSYNC_HTTP_H
