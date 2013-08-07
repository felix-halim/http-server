
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

static const string RESPONSE_200 = "HTTP/1.1 200 OK\r\n" \
  "Content-Type: application/json; charset=utf-8\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n" \
  "Access-Control-Allow-Headers: X-Requested-With\r\n";
static const string RESPONSE_400 = "HTTP/1.1 400 URL Request Error\r\nContent-Length: 0\r\n\r\n";
static const string RESPONSE_500 = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
static const string COMPLETE_FAKE_RESPONSE = "HTTP/1.1 200 OK\r\n" \
  "Content-Type: application/json; charset=utf-8\r\n" \
  "Access-Control-Allow-Origin: *\r\n"  \
  "Content-Length: 11\r\n\r\n{\"hello\":1}\r\n";

static constexpr int MAX_LEN = 1023;
static constexpr int MAX_BUFFER_LEN = 64 * 1024;

static void clear_ss(stringstream &ss) { ss.clear(); ss.str(""); }

void ObjectPool::init(int index) {
  assert(index_ == -1 && index != -1);
  index_ = index;
  start_time_ = system_clock::now();
}

unsigned long long ObjectPool::elapsed() {
  return duration_cast<milliseconds>(system_clock::now() - start_time_).count();
}



// Objects created from this pool is never destroyed.
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

  void release(int idx) {
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



enum class State {
  READING_URL,
  READING_HEADER_FIELD,
  READING_HEADER_VALUE,
  CLOSED,
};

class Connection : public ObjectPool {
  stringstream temp_hf_; // Header field.
  stringstream temp_hv_; // Header value.
  stringstream url_;     // Request URL.
  stringstream body_;    // Request body.

  Server *server_;       // The server that created this connection object.
  Request request_;

  uv_tcp_t handle_;
  http_parser parser_;
  State state_;

  ObjectsPool<Response> *responses_pool;

 public:

  Connection() {
    handle_.data = this;
    parser_.data = this;
  }

  Request* request() { return &request_; }
  State state() { return state_; }
  void set_state(State state) { state_ = state; }
  uv_tcp_t* handle() { return &handle_; }
  http_parser* parser() { return &parser_; }

  void set_server(Server *server) {
    assert(server);
    responses_pool = new ObjectsPool<Response>(server, "Connection");
    server_ = server;
  }

  bool responses_pool_is_empty() { return responses_pool->empty(); }
  Server* server() { return server_; }

  Response* acquire_response() {
    Response *res = responses_pool->acquire();
    res->reset();
    res->set_connection(this);
    return res;
  }
  void release_response(Response *res) {
    responses_pool->release(res->index());
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
    Log::info("append_url %.*s", len, p);
    assert(state_ == State::READING_URL);
    url_.write(p, len);
    return 0;
  }

  int append_header_field(const char *p, size_t len) {
    Log::info("append_hf %.*s", len, p);
    if (state_ == State::READING_URL) state_ = State::READING_HEADER_FIELD;
    finish_header_parsing();
    temp_hf_.write(p, len);
    return 0;
  }

  int append_header_value(const char *p, size_t len) {
    Log::info("append_hv %.*s", len, p);
    assert(state_ != State::READING_URL);
    assert(state_ != State::READING_HEADER_VALUE);
    if (state_ == State::READING_HEADER_FIELD) state_ = State::READING_HEADER_VALUE;
    temp_hv_.write(p, len);
    return 0;
  }

  int append_body(const char *p, size_t len) {
    Log::info("append_body %.*s", len, p);
    body_.write(p, len);
    return 0;
  }

  bool parse(const char *buf, ssize_t nread) {
    assert(server_);
    Log::info("parse %.*s", nread, buf);
    Log::info("server_ %p, %p", server_, server_->get_parser_settings());
    ssize_t parsed = http_parser_execute(&parser_, server_->get_parser_settings(), buf, nread);
    Log::info("server_ %p", server_);
    assert(parsed <= nread);
    return parsed == nread;
  }

  void finish_header_parsing() {
    if (state_ == State::READING_HEADER_VALUE) {
      request_.set_header(temp_hf_.str(), temp_hv_.str());
      clear_ss(temp_hf_);
      clear_ss(temp_hv_);
      state_ = State::READING_HEADER_FIELD;
    }
  }
};


void Response::set_connection(Connection *connection) {
  assert(connection);
  connection_ = connection;
}




static int on_url(http_parser *parser, const char *p, size_t len){ 
  Log::info("UUUU");
  assert(parser->data); return ((Connection*) parser->data)->append_url(p, len); }
static int on_header_field(http_parser *parser, const char *at, size_t len){ Log::info("UUUU");assert(parser->data); return ((Connection*) parser->data)->append_header_field(at, len); }
static int on_header_value(http_parser *parser, const char *at, size_t len){ Log::info("UUUU");assert(parser->data); return ((Connection*) parser->data)->append_header_value(at, len); }
static int on_headers_complete(http_parser* parser) { assert(parser->data); Log::info("UUUU");return ((Connection*) parser->data)->append_header_field("", 0); }
static int on_body(http_parser *parser, const char *p, size_t len){ Log::info("UUUU");assert(parser->data); return ((Connection*) parser->data)->append_body(p, len); }

static bool is_prefix_of(const string &prefix, const string &str) {
  auto res = std::mismatch(prefix.begin(), prefix.end(), str.begin());
  return res.first == prefix.end();
}
// Converts a hex character to its integer value.
static char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}
// Returns a url-decoded version of str.
static void url_decode(char *str, char *buf) {
  char *pstr = str, *pbuf = buf;
  while (*pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    } else if (*pstr == '+') { 
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
  }
  *pbuf = '\0';
}
// static void parse_url(Request *c) {
//   struct http_parser_url url_parser;
//   c->path = "";
//   c->query = "";
//   int len = c->url.length();
//   char url[len + 1];
//   strcpy(url, c->url.c_str());
//   if (!http_parser_parse_url(url, len, 0, &url_parser) && url_parser.field_set & (1<<UF_PATH)) {
//     char *path = url + url_parser.field_data[UF_PATH].off;
//     char *query = (url_parser.field_set & (1<<UF_QUERY)) ? (url + url_parser.field_data[UF_QUERY].off) : NULL;
//     path[url_parser.field_data[UF_PATH].len] = '\0';
//     url_decode(path, path);
//     c->path = path;
//     if (query) {
//       url_decode(query, query);
//       c->query = query;
//     }
//   }
// }
static int on_message_complete(http_parser *parser) {
  Log::info("on_message_complete");
  Connection *c = (Connection*) parser->data;
  c->finish_header_parsing();
  c->server()->varz_inc("server_on_message_complete");
  // parse_url(c);
  c->server()->process(*c->request(), *c->acquire_response());
  assert(c->state() != State::CLOSED);
  c->reset();
  return 0; // stop parsing
}
static void varz_handler(Request &req, Response &res) {
  res.connection()->server()->varz_print(res.body);
  res.send();
}
Server::Server() {
  parser_settings.on_url = on_url;
  parser_settings.on_header_field = on_header_field;
  parser_settings.on_header_value = on_header_value;
  parser_settings.on_headers_complete = on_headers_complete;
  parser_settings.on_body = on_body;
  parser_settings.on_message_complete = on_message_complete;
  connections_pool = new ObjectsPool<Connection>(this, "Connection");
  get("/varz", varz_handler);
  Log::info("server %p; %p", this, &parser_settings);
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

void Server::process(Request &req, Response &res) {
  for (auto &it : handlers) {
    if (is_prefix_of(it.first, req.prefix())) {
      res.set_prefix(req.prefix());
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

const http_parser_settings* Server::get_parser_settings() { return &parser_settings; }

static char buffer[MAX_BUFFER_LEN], buffer_is_used = 0;
static uv_buf_t on_alloc(uv_handle_t* h, size_t suggested_size) {
  assert(suggested_size == MAX_BUFFER_LEN); // libuv dependent code.
  assert(!buffer_is_used);
  buffer_is_used = 1;
  return uv_buf_init(buffer, MAX_BUFFER_LEN);
}
static void try_release_connection(Connection *c) {
  if (c->state() == State::CLOSED && c->responses_pool_is_empty()) {
    c->server()->release_connection(c);
  }
}
static void on_close(uv_handle_t* handle) {
  Connection *c = (Connection*) handle->data;
  assert(c && c->state() != State::CLOSED);
  c->set_state(State::CLOSED);
  try_release_connection(c);
}
static void on_read(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf) {
  Log::info("on_read");
  assert(buffer_is_used);
  buffer_is_used = 0;
  Connection* c = (Connection*) tcp->data;
  assert(c);
  Log::info("on_read %p", c);
  if (nread >= 0) {
    if (!c->parse(buf.base, nread)) uv_close((uv_handle_t*) c->handle(), on_close);
  } else {
    uv_close((uv_handle_t*) c->handle(), on_close);
  }
  Log::info("on_read2");
}
static void on_connect(uv_stream_t* server_handle, int status) {
  Log::info("on_connect");
  Server *server = (Server*) server_handle->data;
  assert(server && !status);
  server->varz_inc("server_on_connect");
  Connection* c = server->acquire_connection();
  c->reset();
  Log::info("on_connect %p", c);
  uv_tcp_init(uv_default_loop(), c->handle());
  status = uv_accept(server_handle, (uv_stream_t*)c->handle());
  assert(!status);
  http_parser_init(c->parser(), HTTP_REQUEST);
  uv_read_start((uv_stream_t*)c->handle(), on_alloc, on_read);
  Log::info("on_connect1");
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
  connections_pool->release(c->index());
}


void Response::reset() {
  body.clear();
  body.str("");
  is_sent_ = false;
}


static void after_write(uv_write_t* req, int status) {
  // varz_inc("server_after_write");
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
  Log::info("send");
  assert(!is_sent_); is_sent_ = true;
  Connection *c = connection();
  c->server()->varz_inc("Response_send");
  assert(!c->responses_pool_is_empty());
  if (c->state() != State::CLOSED) {
    buffer = NULL;
    write_req.data = this;
    uv_buf_t resbuf;
    if (expected_runtime < -1.5) {
      resbuf = (uv_buf_t){ (char*) RESPONSE_500.c_str(), RESPONSE_500.length() };
    } else if (expected_runtime < -0.5) {
      resbuf = (uv_buf_t){ (char*) RESPONSE_400.c_str(), RESPONSE_400.length() };
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
