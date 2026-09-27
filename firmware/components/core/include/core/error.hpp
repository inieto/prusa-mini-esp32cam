// Error model for the portable core: no exceptions, std::expected everywhere.
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace core {

enum class Errc : uint8_t {
  InvalidArgument,
  OutOfRange,
  NoMemory,
  Busy,
  Timeout,
  Io,
  Transport,     // DNS, TCP, TLS or socket failure: the server was not reached
  Unauthorized,  // 401/403: credentials rejected, retrying will not help
  HttpClient,    // other 4xx
  HttpServer,    // 5xx
  NotFound,
  Unsupported,
};

struct Error {
  Errc code;
  int32_t detail = 0;  // HTTP status, esp_err_t, errno… depending on the source
};

template <class T>
using Result = std::expected<T, Error>;

constexpr std::unexpected<Error> fail(Errc code, int32_t detail = 0) {
  return std::unexpected(Error{code, detail});
}

constexpr std::string_view to_string(Errc code) {
  switch (code) {
    case Errc::InvalidArgument: return "invalid argument";
    case Errc::OutOfRange: return "out of range";
    case Errc::NoMemory: return "no memory";
    case Errc::Busy: return "busy";
    case Errc::Timeout: return "timeout";
    case Errc::Io: return "i/o error";
    case Errc::Transport: return "network unreachable";
    case Errc::Unauthorized: return "unauthorized";
    case Errc::HttpClient: return "rejected by server";
    case Errc::HttpServer: return "server error";
    case Errc::NotFound: return "not found";
    case Errc::Unsupported: return "unsupported";
  }
  return "unknown";
}

}  // namespace core
