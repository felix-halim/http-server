#include <cstdio>
#include <vector>

#include "http_server.h"

using namespace std;
using namespace http;

static void add(Request &req, Response &res) {
  int a;
  vector<int> b;

  Log::info("url = %s", req.url().c_str());
  UrlParser url(req.url());
  url >> res.prefix().c_str(); // Match the request prefix.
  url >> a;
  url >> "/";
  url >> b;

  if (url.has_error()) return res.send(-1);

  for (int i : b) a *= i;
  res.body << "hello " << a;

  res.send();
}

int main(int argc, char *argv[]) {
  Server app;

  // Beware one is the prefix of another.
  app.get("/add/", add);

  // Starts the server.
  app.listen("0.0.0.0", 8000);
}
