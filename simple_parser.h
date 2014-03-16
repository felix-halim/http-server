#ifndef SIMPLE_HTTP_PARSER_
#define SIMPLE_HTTP_PARSER_

#include "http_parser.h"
#include "http_server.h"
#include "uv.h"

#include <functional>

namespace simple_http {

// HttpParser's state.
enum class HttpParserState {
  READING_URL,
  READING_HEADER_FIELD,
  READING_HEADER_VALUE,
  CLOSED,
};

constexpr int MAX_BUFFER_LEN = 64 * 1024;

class HttpParser {
 public:
  HttpParser();
  ~HttpParser();

  // Start reading from the stream, and calls on_message_complete callback when a complete http request is received.
  // The callback may be called multiple times (i.e., http pipelining) until the stream is closed.
  void start(
    uv_stream_t *stream,
    enum http_parser_type type,
    std::function<void(Request&)> on_message_complete,
    std::function<void()> on_close);

 // private:
  int append_url(const char *p, size_t len);
  int append_header_field(const char *p, size_t len);
  int append_header_value(const char *p, size_t len);
  int append_body(const char *p, size_t len);

  bool parse(const char *buf, ssize_t nread);

  void reset();                         // Prepare the HttpParser for the next request.
  void build_request();                 // Make the request ready for consumption.
  void close();

  uv_stream_t* tcp;                     // Not owned, passed in through start(), used for close().
  http_parser_settings parser_settings; // Built-in implementation of parsing http requests.
  char buffer[MAX_BUFFER_LEN];          // Reading buffer.
  ostringstream temp_hf_;               // Header field.
  ostringstream temp_hv_;               // Header value.
  ostringstream url_;                   // Request URL.
  ostringstream body_;                  // Request body.
  http_parser parser;                   // HTTP parser to parse the client http requests.
  Request request;                      // Previous Calls to parse() are used to build this Request.
  std::function<void(Request&)> msg_cb; // Callback on message complete.
  std::function<void()> close_cb;       // Callback on close.
  HttpParserState state;                // The state of the current parsing request.
};

}

#endif
