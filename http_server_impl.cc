#include "http_server_impl.h"

#include <assert.h>

namespace simple_http {

using std::chrono::duration_cast;
using std::chrono::milliseconds;

static int on_url(http_parser* parser, const char* p, size_t len) {
  Connection* c = static_cast<Connection*>(parser->data);
  return c->append_url(p, len);
}

static int on_header_field(http_parser* parser, const char* at, size_t len) {
  Connection* c = static_cast<Connection*>(parser->data);
  return c->append_header_field(at, len);
}

static int on_header_value(http_parser* parser, const char* at, size_t len) {
  Connection* c = static_cast<Connection*>(parser->data);
  return c->append_header_value(at, len);
}

static int on_headers_complete(http_parser* parser) {
  Connection* c = static_cast<Connection*>(parser->data);
  return c->append_header_field("", 0);
}

static int on_body(http_parser* parser, const char* p, size_t len) {
  Connection* c = static_cast<Connection*>(parser->data);
  return c->append_body(p, len);
}

static bool is_prefix_of(const string &prefix, const string &str) {
  auto res = std::mismatch(prefix.begin(), prefix.end(), str.begin());
  return res.first == prefix.end();
}

static int on_message_complete(http_parser* parser) {
  Connection* c = static_cast<Connection*>(parser->data);
  assert(c->state != ConnectionState::CLOSED);
  c->build_request();
  c->server->varz.inc("server_on_message_complete");

  for (auto &it : c->server->handlers) {
    if (is_prefix_of(it.first, c->request.url)) {
      // Handle the request.
      Response res { new ResponseImpl(c, it.first) };
      c->server->varz.inc("server_response_impl_alloc");
      c->create_response();
      it.second(c->request, res);
      // Recycle the connection and request object.
      c->reset();
      return 0; // Continue parsing.
    }
  }

  // No handler for the request, send 404 error.
  Response res { new ResponseImpl(c, "/unknown") };
  c->server->varz.inc("server_response_impl_alloc");
  c->create_response();
  res.body() << "Request not found for " << c->request.url;
  res.send(400);
  c->reset(); // Recycle the connection and request object.
  return 0;   // Continue parsing.
}

ServerImpl::ServerImpl() {
  memset(&parser_settings, 0, sizeof(http_parser_settings));
  parser_settings.on_url = on_url;
  parser_settings.on_header_field = on_header_field;
  parser_settings.on_header_value = on_header_value;
  parser_settings.on_headers_complete = on_headers_complete;
  parser_settings.on_body = on_body;
  parser_settings.on_message_complete = on_message_complete;
  get("/varz", [&](Request& req, Response& res) {
    varz.print_to(res.body());
    res.send();
  });
}



void ServerImpl::get(string path, Handler handler) {
  for (auto &it : handlers) {
    if (is_prefix_of(it.first, path)) {
      Log::severe("Path '%s' cannot be the prefix of path '%s'", it.first.c_str(), path.c_str());
      abort();
    }
  }
  handlers.push_back(make_pair(path, handler));
}



static constexpr int MAX_BUFFER_LEN = 64 * 1024;

static char buffer[MAX_BUFFER_LEN], buffer_is_used = 0;

static void on_alloc(uv_handle_t* h, size_t suggested_size, uv_buf_t *buf) {
  assert(suggested_size == MAX_BUFFER_LEN); // libuv dependent code.
  assert(!buffer_is_used);
  buffer_is_used = 1;
  buf->base = buffer;
  buf->len = MAX_BUFFER_LEN;
}

static void try_release_connection(Connection *c) {
}

static void on_close(uv_handle_t* handle) {
  Connection* c = static_cast<Connection*>(handle->data);
  assert(c && c->state != ConnectionState::CLOSED);
  c->state = ConnectionState::CLOSED;
  if (c->disposeable()) {
    c->server->varz.inc("server_connection_dealloc");
    delete c;
  }
}

static void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf) {
  assert(buffer_is_used);
  buffer_is_used = 0;
  Connection* c = static_cast<Connection*>(tcp->data);
  assert(c);
  if (nread < 0 || !c->parse(buf->base, nread)) {
    uv_close((uv_handle_t*) &c->handle, on_close);
  }
}

static void on_connect(uv_stream_t* server_handle, int status) {
  ServerImpl *server = static_cast<ServerImpl*>(server_handle->data);
  assert(server && !status);
  server->varz.inc("server_server_on_connect");
  Connection* c = new Connection(server);
  c->server->varz.inc("server_connection_alloc");
  c->reset();
  uv_tcp_init(uv_default_loop(), &c->handle);
  status = uv_accept(server_handle, (uv_stream_t*) &c->handle);
  assert(!status);
  http_parser_init(&c->parser, HTTP_REQUEST);
  uv_read_start((uv_stream_t*) &c->handle, on_alloc, on_read);
}

void ServerImpl::listen(string address, int port) {
  signal(SIGPIPE, SIG_IGN);
  uv_tcp_t server;
  server.data = this;
  int status = uv_tcp_init(uv_default_loop(), &server);
  assert(!status);
  struct sockaddr_in addr;
  status = uv_ip4_addr(address.c_str(), port, &addr);
  assert(!status);
  status = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  assert(!status);
  uv_listen((uv_stream_t*)&server, 128, on_connect);
  Log::info("Listening on port %d", port);
  varz.set("server_start_time", time(NULL));
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}



#define CRLF "\r\n"
#define CORS_HEADERS                                       \
  "Content-Type: application/json; charset=utf-8"     CRLF \
  "Access-Control-Allow-Origin: *"                    CRLF \
  "Access-Control-Allow-Methods: GET, POST, OPTIONS"  CRLF \
  "Access-Control-Allow-Headers: X-Requested-With"    CRLF

static void after_write(uv_write_t* req, int status) {
  ResponseImpl* res = static_cast<ResponseImpl*>(req->data);
  assert(res);
  res->connection()->server->varz.inc("server_response_impl_dealloc");
  delete res;
}

static int sstream_length(stringstream &ss) {
  ss.seekg(0, std::ios::end);
  int length = ss.tellg();
  ss.seekg(0, std::ios::beg);
  return length;
}

ResponseImpl::ResponseImpl(Connection *con, string req_url):
  c(con), url(req_url), start_time(system_clock::now()), send_buffer({nullptr, 0}) {}

ResponseImpl::~ResponseImpl() {
  if (send_buffer.base) {
    c->server->varz.inc("server_send_buffer_dealloc");
    delete[] send_buffer.base;
    send_buffer.base = NULL;
  }
  c->destroy_response();
  if (c->disposeable()) {
    c->server->varz.inc("server_connection_dealloc");
    delete c;
  }
}

void ResponseImpl::send(int code, int max_age_in_seconds, int expected_runtime_ms) {
  assert(!c->disposeable());
  assert(!send_buffer.base);
  c->server->varz.inc("server_response_send");
  if (c->state == ConnectionState::CLOSED) {
    c->server->varz.inc("server_response_impl_dealloc");
    delete this;
    return;
  }

  auto t1 = system_clock::now();

  stringstream ss; 
  switch (code) {
    case 200: ss << "HTTP/1.1 200 OK" CRLF CORS_HEADERS; break;
    case 400: ss << "HTTP/1.1 400 URL Request Error" CRLF CORS_HEADERS; break;
    case 500: ss << "HTTP/1.1 500 Internal Server Error" CRLF CORS_HEADERS; break;
    default: Log::severe("unknown code %d", code); assert(0); break;
  }
  ss << "Content-Length: " << sstream_length(body) << "\r\n";
  if (max_age_in_seconds > 0) ss << "Cache-Control: no-transform,public,max-age=" << max_age_in_seconds << "\r\n";
  ss << "\r\n" << body.str() << "\r\n";

  string s = ss.str();
  send_buffer.base = new char[send_buffer.len = s.length()];
  c->server->varz.inc("server_send_buffer_alloc");
  memcpy(send_buffer.base, s.data(), send_buffer.len);
  c->server->varz.inc("server_sent_bytes", send_buffer.len);

  auto t2 = system_clock::now();

  auto ms1 = duration_cast<milliseconds>(t1 - start_time).count();
  auto ms2 = duration_cast<milliseconds>(t2 - t1).count();
  auto ms = duration_cast<milliseconds>(t2 - start_time).count();
  c->server->varz.latency("server_process", ms1);
  c->server->varz.latency("server_serialize", ms2);
  c->server->varz.latency("server_response", ms);
  c->server->varz.latency(url, ms);
  if (ms >= expected_runtime_ms) {
    Log::warn("runtime =%6.3lf + %6.3lf = %6.3lf, prefix = %s", ms1 * 1e-3, ms2 * 1e-3, ms * 1e-3, url.c_str());
  }

  write_req.data = this;
  int error = uv_write((uv_write_t*) &write_req, (uv_stream_t*) &c->handle, &send_buffer, 1, after_write);
  if (error) Log::severe("Could not write %d for request %s", error, url.c_str());
}




Connection::Connection(ServerImpl *s): server(s) {
  handle.data = this;
  parser.data = this;
}

Connection::~Connection() {}

static void clear_ss(stringstream &ss) { ss.clear(); ss.str(""); }

void Connection::reset() {
  state = ConnectionState::READING_URL;
  clear_ss(temp_hf_);
  clear_ss(temp_hv_);
  clear_ss(url_);
  clear_ss(body_);
  request.clear();
}

int Connection::append_url(const char *p, size_t len) {
  assert(state == ConnectionState::READING_URL);
  url_.write(p, len);
  return 0;
}

void Connection::build_request() {
  if (state == ConnectionState::READING_URL) {
    request.url = url_.str();
    state = ConnectionState::READING_HEADER_FIELD;
  }
  if (state == ConnectionState::READING_HEADER_VALUE) {
    request.headers[temp_hf_.str()] = temp_hv_.str();
    clear_ss(temp_hf_);
    clear_ss(temp_hv_);
    state = ConnectionState::READING_HEADER_FIELD;
  }
}

int Connection::append_header_field(const char *p, size_t len) {
  build_request();
  temp_hf_.write(p, len);
  return 0;
}

int Connection::append_header_value(const char *p, size_t len) {
  assert(state != ConnectionState::READING_URL);
  assert(state != ConnectionState::READING_HEADER_VALUE);
  if (state == ConnectionState::READING_HEADER_FIELD) {
    state = ConnectionState::READING_HEADER_VALUE;
  }
  temp_hv_.write(p, len);
  return 0;
}

int Connection::append_body(const char *p, size_t len) {
  body_.write(p, len);
  return 0;
}

bool Connection::parse(const char *buf, ssize_t nread) {
  assert(server);
  ssize_t parsed = http_parser_execute(&parser, &server->parser_settings, buf, nread);
  assert(parsed <= nread);
  return parsed == nread;
}

Response Connection::create_response() { active_response++; }
void Connection::destroy_response() { active_response--; }
bool Connection::disposeable() { return state == ConnectionState::CLOSED && !active_response; }

};
