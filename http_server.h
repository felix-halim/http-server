#ifndef _HTTP_SERVER_
#define _HTTP_SERVER_

#include <cassert>
#include <vector>
#include <sstream>
#include <string>
#include <cstring>
#include <map>
#include <unordered_map>
#include <chrono>
#include <algorithm>

class http_parser_settings;

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


  class Request {
    unordered_map<string, string> headers_;
    string url_;
    string body_;

    stringstream url_ss_;
    bool url_read_error_;

   public:

    // Returns the request URL.
    const string url() { return url_; }

    // Returns the request body.
    const string body() { return body_; }

    // Returns the value of the specified header key.
    const string header(string key) const;

    // Read the next integer from the URL.
    Request& operator >>(int &value);

    // Read the next string that matches {@code str} from the URL.
    Request& operator >>(const char *str);

    // Read the next integers (in csv format) from the URL.
    Request& operator >>(std::vector<int> &arr);

    void set_url(string url);
    void set_header(string key, string value) { headers_[key] = value; }
    void clear() { headers_.clear(); url_ = body_ = ""; }
    bool has_error() { return url_read_error_; }
  };



  class ObjectPool {
   protected:
    int index_;
    time_point<system_clock> start_time_;

   public:
    ObjectPool(): index_(-1) {}
    int index() { return index_; }
    void set_index(int index) { index_ = index; }
    void init(int index) {
      assert(index_ == -1 && index != -1);
      index_ = index;
      start_time_ = system_clock::now();
    }
    unsigned long long elapsed() {
      return duration_cast<milliseconds>(system_clock::now() - start_time_).count();
    }
  };

  class Connection;
  class Response : public ObjectPool {
    Connection *connection_;
    string prefix_;
    bool is_sent_;
    void *write_req; // the type is uv_write_t
    char *buffer; // Delete this if not null;

  public:

    Response();
    void reset();
    void delete_buffer() { if (buffer) { delete[] buffer; buffer = NULL; } }

    // Setters.
    const string& prefix() { return prefix_; }
    Connection* connection() { return connection_; }

    // Getters.
    void set_prefix(string prefix) { prefix_ = prefix; }
    void set_connection(Connection*);

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
    http_parser_settings *parser_settings;
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

    // For internal use.
    void process(Request &req, Response &res);
    Connection* acquire_connection();
    void release_connection(Connection*);
    const http_parser_settings* get_parser_settings() { return parser_settings; }
  };
}

#endif
