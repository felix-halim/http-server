#include "http_server_impl.h"
#include <stdarg.h>

namespace simple_http {

// Utility to produce a histogram of request latencies.
class LatencyHistogram {
  int buckets[31];

 public:
  LatencyHistogram() {
    for (int i = 0; i < 31; i++) buckets[i] = 0;
  }
  ~LatencyHistogram() {}

  void add(int us) {
    if (us < 0) Log::severe("Adding negative runtime: %d us", us);
    else buckets[31 - __builtin_clz(std::max(us, 1))]++;
  }
  void print(ostringstream &ss) {
    ss << "[";
    for (int i = 0; i < 30; i++) ss << buckets[i] << ",";
    ss << buckets[30] << "]";
  }
};

class VarzImpl {
 public:

  VarzImpl() {}
  ~VarzImpl() {}

  unsigned long long get(string key) { return varz[key]; }
  void set(string key, unsigned long long value) { varz[key] = value; }
  void inc(string key, unsigned long long value) { varz[key] += value; }
  void latency(string key, int us) {
    if (!varz_hist.count(key)) {
      varz_hist[key] = unique_ptr<LatencyHistogram>(new LatencyHistogram());
    }
    varz_hist[key]->add(us);
  }
  void print_to(ostringstream &ss) {
    ss << "{\n";
    bool first = true;
    for (auto &it : varz) {
      if (first) first = false; else ss << ",\n";
      ss << "\"" << it.first << "\":" << it.second;
    }
    for (auto &it : varz_hist) {
      if (first) first = false; else ss << ",\n";
      ss << "\"" << it.first << "\":";
      it.second->print(ss);
    }
    ss << "\n}\n";
  }

 private:
  map<string, unique_ptr<LatencyHistogram>> varz_hist;
  map<string, unsigned long long> varz;
};


Varz::Varz(): impl(unique_ptr<VarzImpl>(new VarzImpl())) {}
Varz::~Varz() {}
unsigned long long Varz::get(string key) { return impl->get(key); }
void Varz::set(string key, unsigned long long value) { impl->set(key, value); }
void Varz::inc(string key, unsigned long long value) { impl->inc(key, value); }
void Varz::latency(string key, int us) { impl->latency(key, us); }
void Varz::print_to(ostringstream &ss) { impl->print_to(ss); }



Server::Server(): impl(unique_ptr<ServerImpl>(new ServerImpl())) {}
Server::~Server() {}
void Server::get(string prefix, Handler handler) { impl->get(prefix, handler); }
void Server::listen(string address, int port) { impl->listen(address, port); }
Varz* Server::varz() { return &impl->varz; }


Response::Response(ResponseImpl *r): impl(r) {}
Response::~Response() {}
void Response::send(Code code, int max_age_s, int max_runtime_ms) {
  assert(impl);
  impl->send(code, max_age_s, max_runtime_ms);
  impl = nullptr;
}
ostringstream& Response::body() { assert(impl); return impl->body; }


#define LOG_FMT_STDERR(prefix)        \
  va_list args; va_start(args, fmt);  \
  fprintf(stderr, prefix);            \
  vfprintf(stderr, fmt, args);        \
  fprintf(stderr, "\n");              \
  va_end(args);
void Log::severe(const char *fmt, ... ) { LOG_FMT_STDERR("\e[00;31mSEVERE\e[00m ") }
void Log::warn(const char *fmt, ... ) { LOG_FMT_STDERR("\e[1;33mWARN\e[00m ") }
void Log::info(const char *fmt, ... ) { LOG_FMT_STDERR("\e[0;32mINFO\e[00m ") }
#undef LOG_FMT_STDERR

};
