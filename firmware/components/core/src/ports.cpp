#include "core/ports.hpp"

namespace core {

Errc classify_http_status(int status) {
  if (status == 401 || status == 403) return Errc::Unauthorized;
  if (status >= 500 && status <= 599) return Errc::HttpServer;
  return Errc::HttpClient;  // other 4xx, and anything unexpected (1xx/3xx)
}

}  // namespace core
