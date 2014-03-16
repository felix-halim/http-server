#include "simple_parser.h"
#include "uv.h"

namespace simple_http {

static int on_url(http_parser* parser, const char* p, size_t len) {
  return static_cast<HttpParser*>(parser->data)->append_url(p, len);
}

static int on_header_field(http_parser* parser, const char* at, size_t len) {
  return static_cast<HttpParser*>(parser->data)->append_header_field(at, len);
}

static int on_header_value(http_parser* parser, const char* at, size_t len) {
  return static_cast<HttpParser*>(parser->data)->append_header_value(at, len);
}

static int on_headers_complete(http_parser* parser) {
  return static_cast<HttpParser*>(parser->data)->append_header_field("", 0);
}

static int on_body(http_parser* parser, const char* p, size_t len) {
  return static_cast<HttpParser*>(parser->data)->append_body(p, len);
}

static int on_message_complete(http_parser* parser) {
  HttpParser* c = static_cast<HttpParser*>(parser->data);
  assert(c->state != HttpParserState::CLOSED);
  c->build_request();
  c->request.body = c->body_.str();
  // Log::info("on_message_complete parser %p : %s", c, c->request.body.c_str());
  c->msg_cb(c->request);
  c->reset(); // Recycle the HttpParser and request object.
  return 0;   // Continue parsing.
}

HttpParser::HttpParser() {
  memset(&parser_settings, 0, sizeof(http_parser_settings));
  parser_settings.on_url = on_url;
  parser_settings.on_header_field = on_header_field;
  parser_settings.on_header_value = on_header_value;
  parser_settings.on_headers_complete = on_headers_complete;
  parser_settings.on_body = on_body;
  parser_settings.on_message_complete = on_message_complete;

  parser.data = this;

  reset();

  // Log::info("parser created %p", this);
}

HttpParser::~HttpParser() {
  // Log::info("parser destroyed %p", this);
}

static void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t *buf) {
  HttpParser* c = static_cast<HttpParser*>(handle->data);
  assert(suggested_size == MAX_BUFFER_LEN); // libuv dependent code.
  buf->base = c->buffer;
  buf->len = MAX_BUFFER_LEN;
}

static void on_close(uv_handle_t* handle) {
  HttpParser* c = static_cast<HttpParser*>(handle->data);
  assert(c && c->state != HttpParserState::CLOSED);
  c->state = HttpParserState::CLOSED;
  c->close_cb();
  // Log::info("parser on_close %p", c);
}

static void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf) {
  HttpParser* c = static_cast<HttpParser*>(tcp->data);
  assert(c && c->state != HttpParserState::CLOSED);
  // Log::info("on_read parser %p, nread = %d", c, nread);
  if (nread < 0 || !c->parse(buf->base, nread)) {
    c->close();
  }
}

void HttpParser::start(
    uv_stream_t *stream,
    enum http_parser_type type,
    std::function<void(Request&)> on_message_complete,
    std::function<void()> on_close) {
  tcp = stream;
  assert(!stream->data);
  stream->data = this;
  msg_cb = on_message_complete;
  close_cb = on_close;
  http_parser_init(&parser, type);
  uv_read_start(stream, on_alloc, on_read);
}

void HttpParser::close() {
  uv_close((uv_handle_t*) tcp, on_close);
}

static void clear_ss(ostringstream &ss) { ss.clear(); ss.str(""); }

void HttpParser::reset() {
  state = HttpParserState::READING_URL;
  clear_ss(temp_hf_);
  clear_ss(temp_hv_);
  clear_ss(url_);
  clear_ss(body_);
  request.clear();
}

int HttpParser::append_url(const char *p, size_t len) {
  assert(state == HttpParserState::READING_URL);
  url_.write(p, len);
  return 0;
}

void HttpParser::build_request() {
  if (state == HttpParserState::READING_URL) {
    request.url = url_.str();
    state = HttpParserState::READING_HEADER_FIELD;
  }
  if (state == HttpParserState::READING_HEADER_VALUE) {
    request.headers[temp_hf_.str()] = temp_hv_.str();
    clear_ss(temp_hf_);
    clear_ss(temp_hv_);
    state = HttpParserState::READING_HEADER_FIELD;
  }
}

int HttpParser::append_header_field(const char *p, size_t len) {
  build_request();
  temp_hf_.write(p, len);
  return 0;
}

int HttpParser::append_header_value(const char *p, size_t len) {
  assert(state != HttpParserState::READING_URL);
  assert(state != HttpParserState::READING_HEADER_VALUE);
  if (state == HttpParserState::READING_HEADER_FIELD) {
    state = HttpParserState::READING_HEADER_VALUE;
  }
  temp_hv_.write(p, len);
  return 0;
}

int HttpParser::append_body(const char *p, size_t len) {
  // Log::info("body = %.*s", len, p);
  body_.write(p, len);
  return 0;
}

bool HttpParser::parse(const char *buf, ssize_t nread) {
  assert(server);
  ssize_t parsed = http_parser_execute(&parser, &parser_settings, buf, nread);
  assert(parsed <= nread);
  return parsed == nread;
}

}
