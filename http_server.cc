
#include <algorithm>
#include <map>
#include <queue>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "http_server.h"

using namespace std;
using namespace chrono;

namespace http {

#define CRLF "\r\n"
#define CORS_HEADERS                                       \
  "Content-Type: application/json; charset=utf-8"     CRLF \
  "Access-Control-Allow-Origin: *"                    CRLF \
  "Access-Control-Allow-Methods: GET, POST, OPTIONS"  CRLF \
  "Access-Control-Allow-Headers: X-Requested-With"    CRLF
#define RESPONSE_200 "HTTP/1.1 200 OK" CRLF CORS_HEADERS
#define RESPONSE_400 "HTTP/1.1 400 URL Request Error" CRLF CORS_HEADERS \
  "Content-Length: 30" CRLF CRLF "{\"error\":\"URL Request Error\"}\n" CRLF
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error" CRLF CORS_HEADERS \
  "Content-Length: 34" CRLF CRLF "{\"error\":\"Internal Server Error\"}\n" CRLF

static void clear_ss(stringstream &ss) { ss.clear(); ss.str(""); }


// Objects of type T created from this pool are never destroyed, it will be reused.
template<typename T>
class ObjectsPool {
  Server* const server;
  const string name;
  vector<T*> objects;
  queue<int> indices;

 public:

  ObjectsPool(Server *server_, string name_): server(server_), name(name_) {}

  T* acquire() {
    if (indices.empty()) {
      indices.push(objects.size());
      objects.push_back(new T());
      if (objects.size() > 300U) Log::warn("high #%s : %lu", name.c_str(), objects.size());
    }
    assert(server);
    server->varz_inc(name + "_acquire");
    int idx = indices.front();
    indices.pop();
    assert(objects[idx]->index() == -1);
    objects[idx]->init(idx);
    return objects[idx];
  }

  void release(T* obj) {
    int idx = obj->index();
    assert(idx != -1);
    assert(idx >= 0 && idx < (int) objects.size());
    assert(objects[idx]->index() != -1);
    // Log::info("release %s, %d", name, objects[idx]->index());
    objects[idx]->set_index(-1);
    assert(server);
    server->varz_inc(name + "_release");
    server->varz_inc(name + "_elapsed", objects[idx]->elapsed());
    indices.push(idx);
  }

  bool empty() { return indices.size() == objects.size(); }
};



// Utility to produce a histogram of request latencies.
class LatencyHistogram {
  int buckets[31];

 public:
  LatencyHistogram() {
    for (int i = 0; i < 31; i++) buckets[i] = 0;
  }
  void add(int us) {
    if (us < 0) Log::error("Adding negative runtime: %d us", us);
    else buckets[31 - __builtin_clz(max(us, 1))]++;
  }
  void print(stringstream &ss) {
    ss << "[";
    for (int i = 0; i < 30; i++) ss << buckets[i] << ",";
    ss << buckets[30] << "]";
  }
};


// Connection's state.
enum class State {
  READING_URL,
  READING_HEADER_FIELD,
  READING_HEADER_VALUE,
  CLOSED,
};


// A client (browser) request will create a connection object.
// The connection object manages the response objects.
// The server can send multiple responses through this connection object (e.g., http pipelining)
class Connection : public ObjectPool {
  stringstream temp_hf_; // Header field.
  stringstream temp_hv_; // Header value.
  stringstream url_;     // Request URL.
  stringstream body_;    // Request body.

  Server *server_;       // The server that created this connection object.
  Request request_;      // Temporary request object being generated.

  uv_tcp_t handle_;      // TCP connection handle to the client browser.
  http_parser parser_;   // HTTP parser to parse the client http requests.
  State state_;          // The state of the current parsing request.

  // Allocates new (or reuse) existing response object when send() is called.
  ObjectsPool<Response> *responses_pool;

 public:

  Connection() {
    handle_.data = this;
    parser_.data = this;
  }

  // Getters.
  State state() { return state_; }
  Server* server() { return server_; }
  Request* request() { return &request_; }
  uv_tcp_t* handle() { return &handle_; }
  http_parser* parser() { return &parser_; }
  bool responses_pool_is_empty() { return responses_pool->empty(); }

  // Setters.
  void set_state(State state) { state_ = state; }

  void set_server(Server *server) {
    assert(server);
    responses_pool = new ObjectsPool<Response>(server, "Connection");
    server_ = server;
  }

  Response* acquire_response() {
    Response *res = responses_pool->acquire();
    res->reset();
    res->set_connection(this);
    return res;
  }

  void release_response(Response *res) {
    responses_pool->release(res);
  }

  void reset() {
    assert(server_);
    state_ = State::READING_URL;
    clear_ss(temp_hf_);
    clear_ss(temp_hv_);
    clear_ss(url_);
    clear_ss(body_);
    request_.clear();
  }

  int append_url(const char *p, size_t len) {
    assert(state_ == State::READING_URL);
    url_.write(p, len);
    return 0;
  }

  int append_header_field(const char *p, size_t len) {
    finish_header_parsing();
    temp_hf_.write(p, len);
    return 0;
  }

  int append_header_value(const char *p, size_t len) {
    assert(state_ != State::READING_URL);
    assert(state_ != State::READING_HEADER_VALUE);
    if (state_ == State::READING_HEADER_FIELD) state_ = State::READING_HEADER_VALUE;
    temp_hv_.write(p, len);
    return 0;
  }

  int append_body(const char *p, size_t len) {
    body_.write(p, len);
    return 0;
  }

  bool parse(const char *buf, ssize_t nread) {
    assert(server_);
    ssize_t parsed = http_parser_execute(&parser_, server_->get_parser_settings(), buf, nread);
    assert(parsed <= nread);
    return parsed == nread;
  }

  void finish_header_parsing() {
    if (state_ == State::READING_URL) {
      request_.set_url(url_.str());
      state_ = State::READING_HEADER_FIELD;
    }
    if (state_ == State::READING_HEADER_VALUE) {
      request_.set_header(temp_hf_.str(), temp_hv_.str());
      clear_ss(temp_hf_);
      clear_ss(temp_hv_);
      state_ = State::READING_HEADER_FIELD;
    }
  }
};



/***************************************
 Request definitions
 ***************************************/

void Request::set_url(string url) {
  url_read_error_ = false;
  clear_ss(url_ss_);
  url_ss_ << url;
  url_ = url;
}

Request& Request::operator >>(int &value) {
  if (isdigit(url_ss_.peek()) || url_ss_.peek() == '-') url_ss_ >> value;
  else url_read_error_ = true;
  return *this;
}

Request& Request::operator >>(const char *str) {
  char buf[1024];
  int len = strlen(str);
  url_ss_.read(buf, len);
  url_read_error_ |= strncmp(str, buf, len);
  return *this;
}

Request& Request::operator >>(std::vector<int> &arr) {
  char buffer;
  for (int value; !url_read_error_; ) {
    *this >> value;
    if (url_read_error_) break;
    arr.push_back(value);
    if (url_ss_.peek() != ',') break;
    url_ss_.read(&buffer, 1);
  }
  return *this;
}



/***************************************
 Response definitions.
 ***************************************/

void Response::reset() {
  body.clear();
  body.str("");
  is_sent_ = false;
}
void Response::set_connection(Connection *connection) {
  assert(connection);
  connection_ = connection;
}

static void try_release_connection(Connection *c) {
  if (c->state() == State::CLOSED && c->responses_pool_is_empty()) {
    c->server()->release_connection(c);
  }
}
static void after_write(uv_write_t* req, int status) {
  Response *res = (Response*) req->data;
  assert(res);
  res->delete_buffer();
  Connection *c = res->connection();
  c->release_response(res);
  try_release_connection(c);
}
static int sstream_length(stringstream &ss) {
  ss.seekg(0, ios::end);
  int length = ss.tellg();
  ss.seekg(0, ios::beg);
  return length;
}
void Response::send(double expected_runtime, int max_age_in_seconds) {
  assert(!is_sent_); is_sent_ = true;
  Connection *c = connection();
  c->server()->varz_inc("Response_send");
  assert(!c->responses_pool_is_empty());
  if (c->state() != State::CLOSED) {
    buffer = NULL;
    write_req.data = this;
    uv_buf_t resbuf;
    if (expected_runtime < -1.5) {
      resbuf = (uv_buf_t){ (char*) RESPONSE_500, sizeof(RESPONSE_500) / sizeof(RESPONSE_500[0]) };
    } else if (expected_runtime < -0.5) {
      resbuf = (uv_buf_t){ (char*) RESPONSE_400, sizeof(RESPONSE_400) / sizeof(RESPONSE_400[0]) };
    } else {
      unsigned long long runtime_us = elapsed();
      double runtime = runtime_us * 1e-6;
      if (runtime > expected_runtime) Log::warn("runtime = %.3lf, prefix = %s", runtime, prefix_.c_str());

      c->server()->varz_latency(prefix_, runtime_us);

      stringstream ss;
      ss << RESPONSE_200;
      ss << "Content-Length: " << sstream_length(body) << "\r\n";
      if (max_age_in_seconds > 0) {
        ss << "Cache-Control: no-transform,public,max-age=" << max_age_in_seconds << "\r\n";
      }
      ss << "\r\n";

      ss << body.str() << "\r\n";
      string s = ss.str();
      buffer = new char[s.length()];
      memcpy(buffer, s.data(), s.length());
      c->server()->varz_inc("server_sent_bytes", s.length());
      resbuf = (uv_buf_t){ buffer, s.length() };
    }
    int error = uv_write(&write_req, (uv_stream_t*) c->handle(), &resbuf, 1, after_write);
    if (error) {
      Log::error("Could not write %d", error);
      c->release_response(this);
    }
  } else {
    c->release_response(this);
  }
  try_release_connection(c);
}




/***************************************
 Server definitions
 ***************************************/

static int on_url(http_parser *parser, const char *p, size_t len){ return ((Connection*) parser->data)->append_url(p, len); }
static int on_header_field(http_parser *parser, const char *at, size_t len){ return ((Connection*) parser->data)->append_header_field(at, len); }
static int on_header_value(http_parser *parser, const char *at, size_t len){ return ((Connection*) parser->data)->append_header_value(at, len); }
static int on_headers_complete(http_parser* parser) { return ((Connection*) parser->data)->append_header_field("", 0); }
static int on_body(http_parser *parser, const char *p, size_t len){ return ((Connection*) parser->data)->append_body(p, len); }
static int on_message_complete(http_parser *parser) {
  Connection *c = (Connection*) parser->data;
  c->finish_header_parsing();
  c->server()->varz_inc("server_on_message_complete");
  c->server()->process(*c->request(), *c->acquire_response());
  assert(c->state() != State::CLOSED);
  c->reset();
  return 0; // Continue parsing.
}
static void varz_handler(Request &req, Response &res) {
  res.connection()->server()->varz_print(res.body);
  res.send();
}
Server::Server() {
  memset(&parser_settings, 0, sizeof(http_parser_settings));
  parser_settings.on_url = on_url;
  parser_settings.on_header_field = on_header_field;
  parser_settings.on_header_value = on_header_value;
  parser_settings.on_headers_complete = on_headers_complete;
  parser_settings.on_body = on_body;
  parser_settings.on_message_complete = on_message_complete;
  connections_pool = new ObjectsPool<Connection>(this, "Connection");
  get("/varz", varz_handler);
}

void Server::varz_print(stringstream &ss) {
  ss << "{";
  bool first = true;
  for (auto &it : varz) {
    if (first) first = false; else ss << ",";
    ss << "\"" << it.first << "\":" << it.second;
  }
  for (auto &it : varz_hist) {
    if (first) first = false; else ss << ",";
    ss << "\"" << it.first << "\":";
    it.second->print(ss);
  }
  ss << "}";
}

static bool is_prefix_of(const string &prefix, const string &str) {
  auto res = std::mismatch(prefix.begin(), prefix.end(), str.begin());
  return res.first == prefix.end();
}
void Server::process(Request &req, Response &res) {
  for (auto &it : handlers) {
    if (is_prefix_of(it.first, req.url())) {
      res.set_prefix(it.first);
      it.second(req, res);
      return;
    }
  }
  res.send(-1.0);
}
void Server::get(std::string path, Handler handler) {
  for (auto &it : handlers) {
    if (is_prefix_of(it.first, path)) {
      Log::error("Path '%s' cannot be the prefix of path '%s'", it.first.c_str(), path.c_str());
      abort();
    }
  }
  handlers.push_back(make_pair(path, handler));
}


static constexpr int MAX_BUFFER_LEN = 64 * 1024;
static char buffer[MAX_BUFFER_LEN], buffer_is_used = 0;
static uv_buf_t on_alloc(uv_handle_t* h, size_t suggested_size) {
  assert(suggested_size == MAX_BUFFER_LEN); // libuv dependent code.
  assert(!buffer_is_used);
  buffer_is_used = 1;
  return uv_buf_init(buffer, MAX_BUFFER_LEN);
}
static void on_close(uv_handle_t* handle) {
  Connection *c = (Connection*) handle->data;
  assert(c && c->state() != State::CLOSED);
  c->set_state(State::CLOSED);
  try_release_connection(c);
}
static void on_read(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf) {
  assert(buffer_is_used);
  buffer_is_used = 0;
  Connection* c = (Connection*) tcp->data;
  assert(c);
  if (nread >= 0) {
    if (!c->parse(buf.base, nread)) uv_close((uv_handle_t*) c->handle(), on_close);
  } else {
    uv_close((uv_handle_t*) c->handle(), on_close);
  }
}
static void on_connect(uv_stream_t* server_handle, int status) {
  Server *server = (Server*) server_handle->data;
  assert(server && !status);
  server->varz_inc("server_on_connect");
  Connection* c = server->acquire_connection();
  c->reset();
  uv_tcp_init(uv_default_loop(), c->handle());
  status = uv_accept(server_handle, (uv_stream_t*)c->handle());
  assert(!status);
  http_parser_init(c->parser(), HTTP_REQUEST);
  uv_read_start((uv_stream_t*)c->handle(), on_alloc, on_read);
}
void Server::listen(const char *address, int port) {
  signal(SIGPIPE, SIG_IGN);
  uv_tcp_t server;
  server.data = this;
  int status = uv_tcp_init(uv_default_loop(), &server);
  assert(!status);
  status = uv_tcp_bind(&server, uv_ip4_addr(address, port));
  assert(!status);
  uv_listen((uv_stream_t*)&server, 128, on_connect);
  Log::info("Listening on port %d", port);
  varz_set("server_start_time", time(NULL));
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}

Connection* Server::acquire_connection() {
  Connection *c = connections_pool->acquire();
  c->set_server(this);
  return c;
}
void Server::release_connection(Connection *c) {
  connections_pool->release(c);
}
unsigned long long Server::varz_get(string key) { return varz[key]; }
void Server::varz_set(string key, unsigned long long value) { varz[key] = value; }
void Server::varz_inc(string key, unsigned long long value) { varz[key] += value; }
void Server::varz_latency(string key, int us) {
  if (!varz_hist.count(key)) varz_hist[key] = new LatencyHistogram();
  varz_hist[key]->add(us);
}




#define LOG_FMT_STDERR(prefix)        \
  va_list args; va_start(args, fmt);  \
  fprintf(stderr, prefix);            \
  vfprintf(stderr, fmt, args);        \
  fprintf(stderr, "\n");              \
  va_end(args);
void Log::error(const char *fmt, ... ) { LOG_FMT_STDERR("\e[00;31mERROR\e[00m ") }
void Log::warn(const char *fmt, ... ) { LOG_FMT_STDERR("\e[1;33mWARN\e[00m ") }
void Log::info(const char *fmt, ... ) { LOG_FMT_STDERR("\e[0;32mINFO\e[00m ") }
#undef LOG_FMT_STDERR

}
