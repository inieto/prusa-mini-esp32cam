// Web authentication (ADR-0005): PBKDF2-SHA256 password hash in NVS, random session tokens in
// RAM (lost on reboot), brute-force lockout. There is no factory password: the first user is
// created from the UI, and only during the first minutes after boot.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.hpp"
#include "core/time.hpp"

namespace device {

class Auth {
 public:
  static constexpr core::Millis kSetupWindow{15 * 60'000};
  static constexpr core::Millis kSessionLifetime{7LL * 24 * 3600 * 1000};
  static constexpr size_t kMinPasswordLength = 8;

  void load();
  bool setup_required() const { return !configured_; }
  bool setup_window_open(core::Millis now) const { return !configured_ && now < kSetupWindow; }

  core::Result<std::string> setup(std::string_view user, std::string_view password, core::Millis now);
  core::Result<std::string> login(std::string_view user, std::string_view password, core::Millis now);
  bool authenticated(std::string_view cookie_header, core::Millis now) const;
  void logout(std::string_view cookie_header);

  static std::string session_cookie(std::string_view token);
  static constexpr std::string_view kClearCookie =
      "sid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";

 private:
  struct Session {
    std::array<char, 33> token{};  // 128-bit hex + NUL
    core::Millis expires{0};
  };

  bool verify(std::string_view user, std::string_view password) const;
  std::string new_session(core::Millis now);
  static std::optional<std::string_view> cookie_token(std::string_view header);

  bool configured_ = false;
  std::string user_;
  std::array<uint8_t, 16> salt_{};
  std::array<uint8_t, 32> hash_{};
  std::array<Session, 4> sessions_{};
  uint8_t failures_ = 0;
  core::Millis locked_until_{0};
};

}  // namespace device
