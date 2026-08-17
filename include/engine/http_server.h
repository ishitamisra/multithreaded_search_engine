#pragma once
// Level 4.2: "a simple HTTP server to act as a wrapper UI". A minimal,
// dependency-free HTTP/1.1 server built directly on POSIX sockets: one
// accept loop handing connections off to a thread pool. No keep-alive, no
// chunked transfer, no TLS -- deliberately just enough to serve a search
// form and render results, which is what Level 4 asks for.

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>

#include "engine/threadpool.h"

namespace engine {

struct HttpRequest {
  std::string method;
  std::string path;  // decoded, without query string
  std::unordered_map<std::string, std::string> query;  // decoded key/value pairs
};

struct HttpResponse {
  int status = 200;
  std::string contentType = "text/html; charset=utf-8";
  std::string body;
};

class HttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;

  HttpServer(int port, size_t numThreads = 4);
  ~HttpServer();

  void setHandler(Handler handler) { handler_ = std::move(handler); }

  // Blocks, accepting and serving connections, until stop() is called
  // from another thread (e.g. a signal handler).
  void run();
  void stop();

 private:
  void handleConnection(int clientFd);

  int port_;
  int listenFd_ = -1;
  std::atomic<bool> running_{false};
  Handler handler_;
  ThreadPool pool_;
};

std::string urlDecode(const std::string& s);

}  // namespace engine
