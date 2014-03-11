#ifndef SIMPLE_HTTP_SERVER_
#define SIMPLE_HTTP_SERVER_

#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <map>

#include "logger.h"

namespace simple_http {

  using std::map;
  using std::string;
  using std::unique_ptr;
  using std::ostringstream;

  struct Request {
    map<string, string> headers;
    string url;
    string body;

    void clear() {
      headers.clear();
      url = body = "";
    }
  };

  class ResponseImpl;

  // The Response class can be used for asynchronous processing.
  class Response {
   public:
    enum class Code {
      OK,
      NOT_FOUND,
      SERVER_ERROR,
    };

    Response(ResponseImpl*);
    ~Response();

    // Response body. Fill this before calling send() or flush().
    ostringstream& body();

    // Sends the response to the client with the specified error code.
    // No more appends to body allowed after calling send().
    // max_age_s is the number of seconds this request should be cached.
    // A warning is printed if the send() is not called within max_runtime_ms.
    void send(Code code = Code::OK, int max_age_s = 0, int max_runtime_ms = 500);

   private:
    // Not owned by this class.
    ResponseImpl* impl;
  };

  class VarzImpl;

  // Statistis for monitoring.
  class Varz {
   public:
    Varz();
    ~Varz();
    unsigned long long get(string key);
    void set(string key, unsigned long long value);
    void inc(string key, unsigned long long value = 1);
    void latency(string key, int us);
    void print_to(ostringstream &ss);

   private:
    unique_ptr<VarzImpl> impl;
  };

  typedef std::function<void(Request&, Response&)> Handler;

  class ServerImpl;

  class Server {
   public:
    Server();
    ~Server();

    // Non-copy-able.
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Handles http requests where the URL matches the specified prefix.
    void get(string prefix, Handler);

    // Starts the http server at the specified address and port.
    void listen(string address, int port);

    // Server statistics.
    Varz* varz();

   private:
    unique_ptr<ServerImpl> impl;
  };
}

#endif
