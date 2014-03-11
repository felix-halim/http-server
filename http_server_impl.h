#ifndef SIMPLE_HTTP_SERVER_IMPL_
#define SIMPLE_HTTP_SERVER_IMPL_

#include "http_server.h"
#include "simple_parser.h"
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

// One Connection instance per client.
// HTTP pipelining is supported.
class Connection {
 public:
  Connection(ServerImpl*);
  ~Connection();

  ServerImpl *server;       // The server that created this connection object.
  bool cleanup_is_scheduled;
  ResponseImpl* create_response(string prefix);      // Returns a detached object for async response.
  void flush_responses();
  bool disposeable();
  void cleanup();           // Flush all pending responses and destroy this connection if no longer used.

  queue<ResponseImpl*> responses;
  uv_tcp_t handle;          // TCP connection handle to the client browser.
  HttpParser the_parser;    // The parser for the TCP stream handle.
  uv_timer_t timer;         // Cleanup timer.
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

  vector<pair<string, Handler>> handlers;
  Varz varz;
};


};

#endif
