#ifndef SIMPLE_HTTP_SERVER_IMPL_
#define SIMPLE_HTTP_SERVER_IMPL_

#include "http_server.h"
#include "http_parser.h"
#include "uv.h"

#include <assert.h>

#include <chrono>
#include <memory>
#include <vector>

namespace simple_http {

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

  int append_url(const char *p, size_t len);
  int append_header_field(const char *p, size_t len);
  int append_header_value(const char *p, size_t len);
  int append_body(const char *p, size_t len);
  bool parse(const char *buf, ssize_t nread);
  void build_request();
  Request request;          // Previous Calls to parse() are used to build this Request.

  Response create_response();      // Returns a detached object for async response.
  void destroy_response();  // Reclaim the response object.

  bool disposeable();       // Returns true if response object is the connection is closed and response object is no longer used.
  void reset();             // Prepare the connection for the next request.

 private:
  // Temporaries for populating Request instance.
  stringstream temp_hf_; // Header field.
  stringstream temp_hv_; // Header value.
  stringstream url_;     // Request URL.
  stringstream body_;    // Request body.
  int active_response;   // Number of response objects currently active.
};



class ResponseImpl {
 public:
  stringstream body;

  ResponseImpl(Connection *con, string req_url);
  ~ResponseImpl();

  void send(int code, int max_age_in_seconds, int expected_runtime_ms);
  Connection* connection() { return c; }

 private:
  Connection *c; // Not owned.
  string url;
  time_point<system_clock> start_time;
  uv_buf_t send_buffer;
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
