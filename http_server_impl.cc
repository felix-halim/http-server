#include "http_server_impl.h"

#include <assert.h>

namespace simple_http {

using std::chrono::duration_cast;
using std::chrono::milliseconds;

ServerImpl::ServerImpl() {
  get("/varz", [&](Request& req, Response& res) {
    varz.print_to(res.body());
    res.send();
  });
}

static bool is_prefix_of(const string &prefix, const string &str) {
  auto res = std::mismatch(prefix.begin(), prefix.end(), str.begin());
  return res.first == prefix.end();
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

static void on_connect(uv_stream_t* server_handle, int status) {
  ServerImpl *server = static_cast<ServerImpl*>(server_handle->data);
  assert(server && !status);
  Connection* c = new Connection(server);
  c->server->varz.inc("server_connection_alloc");
  uv_tcp_init(uv_default_loop(), &c->handle);
  status = uv_accept(server_handle, (uv_stream_t*) &c->handle);
  assert(!status);

  c->the_parser.start((uv_stream_t*) &c->handle, HTTP_REQUEST,
    [c](Request &req) {
      // On message complete.
      // Log::info("message complete con %p, the_parser = %p, url = %s", c, &c->the_parser, req.url.c_str());
      c->server->varz.inc("server_on_message_complete");
      for (auto &it : c->server->handlers) {
        if (is_prefix_of(it.first, req.url)) {
          // Handle the request.
          Response res { c->create_response(it.first) };
          it.second(req, res);
          return;
        }
      }
      // No handler for the request, send 404 error.
      Response res { c->create_response("/unknown") };
      res.body() << "Request not found for " << req.url;
      res.send(Response::Code::NOT_FOUND);

    }, [c] () {
      // On close.
      // Log::info("Connection closing %p", c);
      c->cleanup();
    });
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
  res->finish();
}

ResponseImpl::ResponseImpl(Connection *con, string req_url):
  c(con), url(req_url), start_time(system_clock::now()), send_buffer({nullptr, 0}), state(0) {}

ResponseImpl::~ResponseImpl() {
  if (send_buffer.base) {
    c->server->varz.inc("server_send_buffer_dealloc");
    delete[] send_buffer.base;
  }
}

void ResponseImpl::send(Response::Code code, int max_age_s, int max_runtime_ms) {
  assert(c);          // send() can only be called exactly once.
  this->state = 1;    // after send().
  this->code = code;
  this->max_age_s = max_age_s;
  this->max_runtime_ms = max_runtime_ms;
  // Log::info("RESPONSE send con = %p, code = %d", c, code);
  c->cleanup();
}

void ResponseImpl::flush(uv_write_cb cb) {
  assert(state == 1);
  state = 2; // after flush().
  auto t1 = system_clock::now();
  assert(c);
  assert(c->state != HttpParserState::CLOSED);
  assert(!c->disposeable());
  c->server->varz.inc("server_response_send");

  ostringstream ss; 
  switch (code) {
    case Response::Code::OK: ss << "HTTP/1.1 200 OK" CRLF CORS_HEADERS; break;
    case Response::Code::NOT_FOUND: ss << "HTTP/1.1 400 URL Request Error" CRLF CORS_HEADERS; break;
    case Response::Code::SERVER_ERROR: ss << "HTTP/1.1 500 Internal Server Error" CRLF CORS_HEADERS; break;
    default: Log::severe("unknown code %d", code); assert(0); break;
  }
  string body_str = body.str();
  ss << "Content-Length: " << body_str.length() << "\r\n";
  if (max_age_s > 0) ss << "Cache-Control: no-transform,public,max-age=" << max_age_s << "\r\n";
  ss << "\r\n" << body_str << "\r\n";

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
  if (ms >= max_runtime_ms) {
    Log::warn("runtime =%6.3lf + %6.3lf = %6.3lf, prefix = %s", ms1 * 1e-3, ms2 * 1e-3, ms * 1e-3, url.c_str());
  }

  write_req.data = this;
  int error = uv_write((uv_write_t*) &write_req, (uv_stream_t*) &c->handle, &send_buffer, 1, cb);
  if (error) Log::severe("Could not write %d for request %s", error, url.c_str());
}



Connection::Connection(ServerImpl *s): server(s) {
  handle.data = this;
  timer.data = this;
  cleanup_is_scheduled = false;
  // Log::warn("Connection created %p", this);
}

Connection::~Connection() {
  // Log::warn("Connection destroyed %p", this);
}

ResponseImpl* Connection::create_response(string prefix) {
  ResponseImpl* res = new ResponseImpl(this, prefix);
  server->varz.inc("server_response_impl_alloc");
  responses.push(res);
  return res;
}

static void cleanup_connection(uv_timer_t* handle, int status) {
  Connection* c = static_cast<Connection*>(handle->data);
  assert(c && c->cleanup_is_scheduled);
  c->cleanup_is_scheduled = false;
  c->flush_responses();
  if (c->disposeable()) {
    c->server->varz.inc("server_connection_dealloc");
    // Log::warn("Connection cleaned up: %p", c);
    delete c;
  }
}

void Connection::cleanup() {
  if (cleanup_is_scheduled) return;
  uv_timer_init(uv_default_loop(), &timer);
  uv_timer_start(&timer, cleanup_connection, 0, 0);
  cleanup_is_scheduled = true;
}

void Connection::flush_responses() {
  while (!responses.empty()) {
    ResponseImpl* res = responses.front();
    if (res->get_state() == 0) return; // Not yet responded.
    if (res->get_state() == 1 && the_parser.state != HttpParserState::CLOSED) return res->flush(after_write);
    if (res->get_state() == 2) return; // Not yet written.
    responses.pop();
    server->varz.inc("server_response_impl_dealloc");
    delete res;
  }
}

bool Connection::disposeable() {
  return the_parser.state == HttpParserState::CLOSED && responses.empty();
}

};
