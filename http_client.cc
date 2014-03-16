#include <string.h>

#include <string>
#include <queue>
#include <algorithm>

#include "http_client.h"
#include "simple_parser.h"
#include "uv.h"

using namespace std;

namespace simple_http {

enum class ClientState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  UNINITED,
  WAITING
};

class ClientImpl {
 public:
  ClientImpl(const char *h, int p);

  void request(const char *url, const string &body, std::function<void(const string&)> response_callback);
  void flush();
  void try_connect();
  void close();

  const string host;
  int port;
  int timeout;
  uv_connect_t connect_req;
  uv_timer_t connect_timer;
  uv_tcp_t handle;
  HttpParser the_parser;    // The parser for the TCP stream handle.

  queue<pair<string, string>> req_queue; // url, body.
  queue<std::function<void(const string&)>> cb_queue;
  ClientState connection_status;
  bool is_idle;
};

static void on_connect(uv_connect_t *req, int status);
static void try_connect_cb(uv_timer_t* handle, int status) {
  ClientImpl *c = (ClientImpl*) handle->data;
  Log::warn("Connecting to %s:%d", c->host.c_str(), c->port);
  c->connection_status = ClientState::CONNECTING;
  uv_tcp_init(uv_default_loop(), &c->handle);
  struct sockaddr_in dest;
  uv_ip4_addr(c->host.c_str(), c->port, &dest);
  int r = uv_tcp_connect(&c->connect_req, &c->handle, (const struct sockaddr*) &dest, on_connect);
  if (r) {
    Log::severe("Failed connecting to %s:%d", c->host.c_str(), c->port);
    on_connect(&c->connect_req, r);
  }
}

static void on_connect(uv_connect_t *req, int status) {
  ClientImpl *c = (ClientImpl*) req->data;
  assert(&c->connect_req == req);
  if (status) {
    Log::severe("on_connect by %s:%d, queue = %lu/%lu\n",
      c->host.c_str(), c->port, c->req_queue.size(), c->cb_queue.size());
    c->connection_status = ClientState::DISCONNECTED;
    c->try_connect();
    c->timeout = min(c->timeout * 2, 8000);
    return;
  }

  c->timeout = 1000;
  Log::info("CONNECTED %s:%d, queue=%d/%d, readable=%d, writable=%d, status=%d, is_idle=%d",
    c->host.c_str(), c->port, c->req_queue.size(), c->cb_queue.size(),
    uv_is_readable(req->handle), uv_is_writable(req->handle), status, c->is_idle);
  c->is_idle = true;
  c->connection_status = ClientState::CONNECTED;

  assert(uv_is_readable(req->handle));
  assert(uv_is_writable(req->handle));
  assert(!uv_is_closing((uv_handle_t*) req->handle));

  // Log::info("CB SIZE = %d", c->cb_queue.size());
  c->the_parser.start((uv_stream_t*) &c->handle, HTTP_RESPONSE,
    [c](Request &req) {
      // On message complete.
      assert(!c->cb_queue.empty());
      c->cb_queue.front()(req.body);
      c->cb_queue.pop();
      c->req_queue.pop();
      c->is_idle = true;
      c->flush();
      // Log::info("complete qsize = %d/%d, %d", 
      //   c->req_queue.size(), c->cb_queue.size(), c->connection_status == ClientState::CONNECTED);

    }, [c] () {
      // On close.
      if (c->connection_status == ClientState::CONNECTED) {
        Log::severe("Connection closed %s:%d, reconnecting", c->host.c_str(), c->port);
        c->connection_status = ClientState::DISCONNECTED;
        c->timeout = 1000;
        c->try_connect();
      } else {
        Log::info("Client DISCONNECTED");
      }
    });

  c->flush();
}

Client::Client(const char *host, int port): impl(unique_ptr<ClientImpl>(new ClientImpl(host, port))) {}
Client::~Client() {}

void Client::request(const char *url, const string &body,
    std::function<void(const string&)> response_callback) {
  impl->request(url, body, response_callback);
}

void Client::close() {
  impl->close();
}

ClientImpl::ClientImpl(const char *h, int p): host(h), port(p) {
  connect_req.data = this;
  connection_status = ClientState::UNINITED;
  connect_timer.data = this;
  timeout = 1000;
  is_idle = false;
}

void ClientImpl::request(const char *url, const string &body, std::function<void(const string&)> response_callback) {
  req_queue.push(make_pair(url, body));
  cb_queue.push(response_callback);
  // Log::info("request qsize = %d/%d, %d",
  //   req_queue.size(), cb_queue.size(), connection_status == ClientState::CONNECTED);
  if (connection_status == ClientState::CONNECTED) {
    flush();
  } else {
    try_connect();
  }
}

static void after_write(uv_write_t *req, int status) {
  // Log::info("after_write");
  assert(status == 0);
  free(req->data);
  free(req);
}

static void write_string(char *s, int length, uv_tcp_t* tcp) {
  // Log::info("writing: %.*s", length, s);
  uv_buf_t buf = uv_buf_init(s, length);
  uv_write_t *req = (uv_write_t*) malloc(sizeof(*req));
  req->data = malloc(length);
  memcpy((char*) req->data, s, length);
  if (uv_write(req, (uv_stream_t*)tcp, &buf, 1, after_write)) {
    Log::severe("uv_write failed");
    assert(0);
  }
}

void ClientImpl::flush() {
  if (connection_status == ClientState::CONNECTED && is_idle && !req_queue.empty()) {
    char url[1024];
    assert(!cb_queue.empty());
    const char *path = req_queue.front().first.c_str();
    // Log::info("flush %s con=%d, qsize=%d", path, connection_status, cb_queue.size());
    if (req_queue.front().second.length()) {
      int length = req_queue.front().second.length();
      sprintf(url, "POST %s HTTP/1.1\r\nContent-Type: multipart/form-data\r\nContent-Length: %d\r\n\r\n", path, length);
      write_string(url, strlen(url), &handle);
      write_string((char*) req_queue.front().second.data(), length, &handle);
      sprintf(url + 1000, "\r\n");
      write_string(url + 1000, strlen(url + 1000), &handle);
    } else {
      sprintf(url, "GET %s HTTP/1.1\r\n\r\n", path);
      write_string(url, strlen(url), &handle);
    }
    is_idle = false;
  } else if (!cb_queue.empty()) {
    Log::info("failed flush %d != %d, %d", connection_status, ClientState::CONNECTED, cb_queue.size());
  }
}

void ClientImpl::try_connect() {
  switch (connection_status) {
    case ClientState::CONNECTED: Log::severe("Already connected"); assert(0); break;
    case ClientState::CONNECTING: Log::severe("Try double connect"); assert(0); break;
    case ClientState::WAITING: Log::info("Try connect: state WAITING"); break;
    case ClientState::DISCONNECTED: Log::info("Try connect: state DISCONNECTED");
      uv_timer_stop(&connect_timer);
    case ClientState::UNINITED: Log::info("Try connect: state UNINITED");
      uv_timer_init(uv_default_loop(), &connect_timer);
      uv_timer_start(&connect_timer, try_connect_cb, timeout, 0);
      connection_status = ClientState::WAITING;
      break;
  }
}

void ClientImpl::close() {
  switch (connection_status) {
    case ClientState::CONNECTED:
      connection_status = ClientState::DISCONNECTED;
      the_parser.close();
      break;
    case ClientState::CONNECTING: Log::severe("CONNECTING"); assert(0); break;
    case ClientState::WAITING: Log::info("WAITING"); assert(0); break;
    case ClientState::DISCONNECTED: Log::info("Already DISCONNECTED"); the_parser.close(); break;
    case ClientState::UNINITED: Log::info("UNINITED"); assert(0); break;
  }
}

}