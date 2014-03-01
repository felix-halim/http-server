#include "http_server.h"

#include <cstring>
#include <vector>
#include <algorithm>

using namespace std;
using namespace simple_http;

// Application Singleton accessible globally.
static Server& app() {
  static Server s;
  return s;
}

// Handler for request: "/add/2,3"
static void add_handler(Request& req, Response& res) {
  const char* url = req.url.c_str();

  // Log to server console.
  Log::info("url = %s", url);

  long long a, b;
  if (sscanf(url, "/add/%lld,%lld", &a, &b) != 2) {
    res.body() << "{\"error\":\"URL Request Error\"}\n";
    app().varz()->inc("Malformed URL");
    return res.send(Response::Code::NOT_FOUND);
  }

  if (a + b > 1000) {
    res.body() << "{\"error\":\"Internal Server Error\"}\n";
    app().varz()->inc("Server Crashed");
    return res.send(Response::Code::SERVER_ERROR);
  }

  res.body() << "a + b = " << a + b << "\n";
  // Send 200 OK.
  res.send();
  app().varz()->inc("OK");
}

static vector<Response> pending;

static void add_async_handler(Request& req, Response& res) {
  const char* url = req.url.c_str();

  // Log to server console.
  Log::info("async url = %s", url);

  long long a, b;
  if (sscanf(url, "/add_async/%lld,%lld", &a, &b) != 2) {
    res.body() << "{\"error\":\"URL Request Error\"}\n";
    app().varz()->inc("Malformed URL");
    return res.send(Response::Code::NOT_FOUND);
  }

  if (a + b > 1000) {
    res.body() << "{\"error\":\"Internal Server Error\"}\n";
    app().varz()->inc("Server Crashed");
    return res.send(Response::Code::SERVER_ERROR);
  }

  res.body() << "a + b = " << a + b << "\n";
  pending.push_back(res);
  // Do not send yet...
}

static void add_flush_handler(Request& req, Response& res) {
  // Clears all pending responses.

  Log::info("Flushing %lu", pending.size());
  reverse(pending.begin(), pending.end());
  for (Response &p : pending) {
    p.send();
    Log::info("Flushed");
    app().varz()->inc("OK Async");
  }

  res.body() << "flushed " << pending.size() << " requests";
  res.send();
  app().varz()->inc("OK Async");
  Log::info("Flushed all");
  pending.clear();
}

int main(int argc, char* argv[]) {
  // Beware one is the prefix of another.
  app().get("/add/", add_handler);
  app().get("/add_async/", add_async_handler);
  app().get("/add_flush", add_flush_handler);

  // Starts the server.
  app().listen("0.0.0.0", 8000);
}
