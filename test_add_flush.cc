#include <stdio.h>

#include <chrono>
#include <thread>

#include "http_client.h"
#include "uv.h"

using namespace std;
using namespace simple_http;

int main() {
  Client c { "127.0.0.1", 8000 };

  c.request("/add_flush", "", [&c] (const string &res) {
    printf("result = %s\n", res.c_str());
    c.close();
  });

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}
