#ifndef _HTTP_SERVER_
#define _HTTP_SERVER_

#include <vector>
#include <sstream>
#include <string>
#include <map>
#include <unordered_map>
#include <chrono>
#include <algorithm>

#include "http_parser.h"
#include "uv.h"

namespace http {

  using namespace std;
  using namespace chrono;

  // Global logging to standard error.
  struct Log {
    static int max_level;
    static void error(const char *fmt, ... );
    static void warn(const char *fmt, ... );
    static void info(const char *fmt, ... );
  };

  class UrlParser {
    stringstream s;
    bool error;

   public:

    UrlParser(string str): error(false) { s << str; }

    bool has_error() { return error; }

    UrlParser& operator >>(int &value) {
      if (isdigit(s.peek()) || s.peek() == '-') s >> value; else error = true;
      return *this;
    }

    UrlParser& operator >>(const char *str) {
      char buf[1024];
      int len = strlen(str);
      s.read(buf, len);
      error |= strncmp(str, buf, len);
      return *this;
    }

    UrlParser& operator >>(std::vector<int> &arr) {
      char buffer;
      for (int value; !error; ) {
        *this >> value;
        if (error) break;
        arr.push_back(value);
        if (s.peek() != ',') break;
        s.read(&buffer, 1);
      }
      return *this;
    }
  };

  class ObjectPool {
   protected:
    int index_;
    time_point<system_clock> start_time_;

   public:
    ObjectPool(): index_(-1) {}
    unsigned long long elapsed();
    int index() { return index_; }
    void set_index(int index) { index_ = index; }
    void init(int index);
  };



  class Request {
    unordered_map<string, string> headers_;
    string url_;
    string body_;

   public:

    // Returns the request URL.
    const string url() { return url_; }

    // Returns the request body.
    const string body() { return body_; }

    // Returns the value of the specified header key.
    const string header(string key) const;

    void set_url(string url) { url_ = url; }
    void set_header(string key, string value) { headers_[key] = value; }
    void clear() { headers_.clear(); url_ = body_ = ""; }
  };



  class Connection;
  class Response : public ObjectPool {
    Connection *connection_;
    string prefix_;
    bool is_sent_;
    uv_write_t write_req;
    char *buffer; // Delete this if not null;

  public:

    void reset();
    void set_connection(Connection*);
    Connection* connection() { return connection_; }
    void delete_buffer() { if (buffer) delete[] buffer; }
    const string& prefix() { return prefix_; }
    void set_prefix(string prefix) { prefix_ = prefix; }

    // Response body. Fill this before calling send().
    stringstream body;

    // Sends the response to the client. Exactly call this ONCE.
    // Set the expected_runtime to -1.0 if error occurs.
    // Set the http cache control if needed.
    void send(double expected_runtime = 0.05, int max_age_in_seconds = 0);
  };


  typedef void (*Handler)(Request &req, Response &res);
  template<typename T> class ObjectsPool;
  class LatencyHistogram;
  class WriteRequest;

  class Server {
    vector<pair<string, Handler>> handlers;
    map<string, LatencyHistogram*> varz_hist;
    map<string, unsigned long long> varz;
    http_parser_settings parser_settings;
    ObjectsPool<Connection> *connections_pool;

   public:

    Server();

    // Handles http requests where the URL matches the specified prefix.
    void get(string prefix, Handler);

    // Starts the http server at the specified address and port.
    void listen(const char *address, int port);

    // Statistical values for monitoring.
    unsigned long long varz_get(string key);
    void varz_set(string key, unsigned long long value);
    void varz_inc(string key, unsigned long long value = 1);
    void varz_latency(string key, int us);
    void varz_print(stringstream &ss);

    const http_parser_settings* get_parser_settings();
    void process(Request &req, Response &res);
    Connection* acquire_connection();
    void release_connection(Connection*);
  };


}

#endif
