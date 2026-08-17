#include "engine/http_server.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace engine {

namespace {
std::string statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Unknown";
  }
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

std::string urlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = hexVal(s[i + 1]);
      int lo = hexVal(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i] == '+' ? ' ' : s[i]);
  }
  return out;
}

namespace {
std::unordered_map<std::string, std::string> parseQueryString(const std::string& qs) {
  std::unordered_map<std::string, std::string> result;
  std::stringstream ss(qs);
  std::string pair;
  while (std::getline(ss, pair, '&')) {
    if (pair.empty()) continue;
    size_t eq = pair.find('=');
    std::string key = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
    std::string value = urlDecode(eq == std::string::npos ? "" : pair.substr(eq + 1));
    result[key] = value;
  }
  return result;
}
}  // namespace

HttpServer::HttpServer(int port, size_t numThreads) : port_(port), pool_(numThreads) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::run() {
  listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) throw std::runtime_error("HttpServer: socket() failed");

  int opt = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(listenFd_);
    throw std::runtime_error("HttpServer: bind() failed on port " +
                              std::to_string(port_));
  }
  if (listen(listenFd_, 64) < 0) {
    close(listenFd_);
    throw std::runtime_error("HttpServer: listen() failed");
  }

  running_ = true;
  while (running_) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientFd < 0) {
      if (!running_) break;
      continue;
    }
    pool_.submit([this, clientFd]() { handleConnection(clientFd); });
  }
}

void HttpServer::stop() {
  if (running_.exchange(false) && listenFd_ >= 0) {
    shutdown(listenFd_, SHUT_RDWR);
    close(listenFd_);
    listenFd_ = -1;
  }
}

void HttpServer::handleConnection(int clientFd) {
  std::string buffer;
  buffer.reserve(4096);
  char chunk[4096];
  // Read until we have the full header block; this server does not read
  // a request body (GET-only API), so headers are all we need.
  for (;;) {
    ssize_t n = recv(clientFd, chunk, sizeof(chunk), 0);
    if (n <= 0) break;
    buffer.append(chunk, n);
    if (buffer.find("\r\n\r\n") != std::string::npos) break;
    if (buffer.size() > 64 * 1024) break;  // guard against pathological input
  }

  HttpResponse response;
  response.status = 400;
  response.body = "Bad Request";

  size_t lineEnd = buffer.find("\r\n");
  if (lineEnd != std::string::npos) {
    std::string requestLine = buffer.substr(0, lineEnd);
    std::istringstream lineStream(requestLine);
    std::string method, target, version;
    lineStream >> method >> target >> version;

    if (!method.empty() && !target.empty()) {
      HttpRequest request;
      request.method = method;
      size_t qmark = target.find('?');
      request.path = urlDecode(qmark == std::string::npos ? target : target.substr(0, qmark));
      if (qmark != std::string::npos) {
        request.query = parseQueryString(target.substr(qmark + 1));
      }
      try {
        response = handler_ ? handler_(request) : HttpResponse{404, "text/plain", "no handler"};
      } catch (const std::exception& e) {
        response = HttpResponse{500, "text/plain", std::string("Internal error: ") + e.what()};
      }
    }
  }

  std::ostringstream out;
  out << "HTTP/1.1 " << response.status << " " << statusText(response.status) << "\r\n";
  out << "Content-Type: " << response.contentType << "\r\n";
  out << "Content-Length: " << response.body.size() << "\r\n";
  out << "Connection: close\r\n\r\n";
  out << response.body;
  std::string outStr = out.str();
  size_t sent = 0;
  while (sent < outStr.size()) {
    ssize_t n = send(clientFd, outStr.data() + sent, outStr.size() - sent, 0);
    if (n <= 0) break;
    sent += static_cast<size_t>(n);
  }
  close(clientFd);
}

}  // namespace engine
