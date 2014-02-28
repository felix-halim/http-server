#ifndef SIMPLE_HTTP_SERVER_IMPL_
#define SIMPLE_HTTP_SERVER_IMPL_

#include "http_server.h"
#include "http_parser.h"
#include "uv.h"

#include <assert.h>

#include <chrono>
#include <memory>
#include <vector>
#include <queue>

namespace simple_http {

using std::queue;
using std::vector;
using std::pair;
using std::chrono::time_point;
using std::chrono::system_clock;

// Connection's state.
enum class ConnectionState {
  READING_URL,
  READING_HEADER_FIELD,
  READING_HEADER_VALUE,
  CLOSED,
};

// One Connection instance per client.
// HTTP pipelining is supported.
class Connection {
 public:
  Connection(ServerImpl*);
  ~Connection();

  ServerImpl *server;       // The server that created this connection object.
  ConnectionState state;    // The state of the current parsing request.
  uv_tcp_t handle;          // TCP connection handle to the client browser.
  http_parser parser;       // HTTP parser to parse the client http requests.
  bool cleanup_is_scheduled;

  int append_url(const char *p, size_t len);
  int append_header_field(const char *p, size_t len);
  int append_header_value(const char *p, size_t len);
  int append_body(const char *p, size_t len);
  bool parse(const char *buf, ssize_t nread);
  void build_request();
  Request request;          // Previous Calls to parse() are used to build this Request.

  ResponseImpl* create_response(string prefix);      // Returns a detached object for async response.
  void flush_responses();
  bool disposeable();

  void cleanup();           // Flush all pending responses and destroy this connection if no longer used.
  void reset();             // Prepare the connection for the next request.

 private:
  // Temporaries for populating Request instance.
  ostringstream temp_hf_; // Header field.
  ostringstream temp_hv_; // Header value.
  ostringstream url_;     // Request URL.
  ostringstream body_;    // Request body.
  queue<ResponseImpl*> responses;
  uv_timer_t timer;
};



class ResponseImpl {
 public:
  ostringstream body;

  ResponseImpl(Connection *con, string req_url);
  ~ResponseImpl();

  // In a pipelined response, this send request will be queued if it's not the head.
  void send(Response::Code code, int max_age_s, int max_runtime_ms);

  // Send the body to client then asynchronously call "cb".
  void flush(uv_write_cb cb);

  Connection* connection() { return c; }
  int get_state() { return state; }
  void finish() {
    assert(state == 2);
    state = 3;
    c->cleanup();
  }

 private:
  Connection *c; // Not owned.
  string url;
  time_point<system_clock> start_time;
  uv_buf_t send_buffer;
  int state; // 0 = initialized, 1 = after send(), 2 = after flush(), 3 = finished
  Response::Code code;
  int max_age_s;
  int max_runtime_ms;
  uv_write_t write_req;
};



class ServerImpl {
 public:
  ServerImpl();
  void get(string path, Handler handler);
  void listen(string address, int port);

  http_parser_settings parser_settings;
  vector<pair<string, Handler>> handlers;
  Varz varz;
};


};

#endif
