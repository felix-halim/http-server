#ifndef SIMPLE_HTTP_CLIENT_
#define SIMPLE_HTTP_CLIENT_

#include <memory>
#include <sstream>

#include "logger.h"

namespace simple_http {

using std::string;
using std::unique_ptr;

class ClientImpl;

class Client {
 public:

  Client(const char *addr, int port = 80);
  ~Client();

  void request(const char *url, const string &body,
    std::function<void(const string&)> response_callback);

  void close();

 private:
  unique_ptr<ClientImpl> impl;
};

}

#endif
