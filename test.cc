#include <cstdio>
#include <vector>

#include "http_server.h"

using namespace std;
using namespace http;

static void add(Request &req, Response &res) {
  int a;
  vector<int> b;

  req >> res.prefix().c_str(); // Match the request prefix.
  req >> a;
  req >> "/";
  req >> b;

  if (req.has_error()) return res.send(-1);

  for (int i : b) a *= i;
  res.body << "hello " << a << "\n";

  Log::info("url = %s, a = %d", req.url().c_str(), a);
  if (a > 1000) return res.send(-2);

  res.send();
}

int main(int argc, char *argv[]) {
  Server app;

  // Beware one is the prefix of another.
  app.get("/add/", add);

  // Starts the server.
  app.listen("0.0.0.0", 8000);
}
