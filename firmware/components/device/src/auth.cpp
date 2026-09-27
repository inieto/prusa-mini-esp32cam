#include "device/auth.hpp"

#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/pkcs5.h>
#include <nvs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace device {
namespace {

constexpr const char* kTag = "auth";
constexpr unsigned kIterations = 4096;  // ~0.3 s on the ESP32 SHA accelerator

std::array<uint8_t, 32> derive(std::string_view password, const std::array<uint8_t, 16>& salt) {
  std::array<uint8_t, 32> out{};
  mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                reinterpret_cast<const unsigned char*>(password.data()),
                                password.size(), salt.data(), salt.size(), kIterations,
                                out.size(), out.data());
  return out;
}

bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

}  // namespace

void Auth::load() {
  nvs_handle_t h;
  if (nvs_open("web", NVS_READONLY, &h) != ESP_OK) return;
  char user[33] = {};
  size_t len = sizeof user;
  size_t salt_len = salt_.size(), hash_len = hash_.size();
  configured_ = nvs_get_str(h, "user", user, &len) == ESP_OK &&
                nvs_get_blob(h, "salt", salt_.data(), &salt_len) == ESP_OK &&
                nvs_get_blob(h, "hash", hash_.data(), &hash_len) == ESP_OK &&
                salt_len == salt_.size() && hash_len == hash_.size();
  user_ = user;
  nvs_close(h);
}

core::Result<std::string> Auth::setup(std::string_view user, std::string_view password,
                                      core::Millis now) {
  if (!setup_window_open(now)) return core::fail(core::Errc::Unauthorized);
  if (user.empty() || user.size() > 32 || password.size() < kMinPasswordLength ||
      password.size() > 64) {
    return core::fail(core::Errc::InvalidArgument);
  }
  esp_fill_random(salt_.data(), salt_.size());
  hash_ = derive(password, salt_);
  user_ = std::string(user);

  nvs_handle_t h;
  if (nvs_open("web", NVS_READWRITE, &h) != ESP_OK) return core::fail(core::Errc::Io);
  nvs_set_str(h, "user", user_.c_str());
  nvs_set_blob(h, "salt", salt_.data(), salt_.size());
  nvs_set_blob(h, "hash", hash_.data(), hash_.size());
  const esp_err_t err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) return core::fail(core::Errc::Io, err);

  configured_ = true;
  ESP_LOGI(kTag, "web user '%s' created", user_.c_str());
  return new_session(now);
}

bool Auth::verify(std::string_view user, std::string_view password) const {
  const auto candidate = derive(password, salt_);
  const bool user_ok = user.size() == user_.size() &&
                       constant_time_equal(reinterpret_cast<const uint8_t*>(user.data()),
                                           reinterpret_cast<const uint8_t*>(user_.data()),
                                           user.size());
  const bool pass_ok = constant_time_equal(candidate.data(), hash_.data(), hash_.size());
  return user_ok && pass_ok;
}

core::Result<std::string> Auth::login(std::string_view user, std::string_view password,
                                      core::Millis now) {
  if (!configured_) return core::fail(core::Errc::Unauthorized);
  if (now < locked_until_) return core::fail(core::Errc::Busy);
  if (!verify(user, password)) {
    if (++failures_ >= 5) {
      failures_ = 0;
      locked_until_ = now + core::Millis{30'000};
      ESP_LOGW(kTag, "too many failed logins, locked for 30 s");
    }
    return core::fail(core::Errc::Unauthorized);
  }
  failures_ = 0;
  return new_session(now);
}

std::string Auth::new_session(core::Millis now) {
  Session* slot = &sessions_[0];
  for (auto& s : sessions_) {
    if (s.expires < slot->expires) slot = &s;  // reuse free/expired/oldest
  }
  uint8_t random[16];
  esp_fill_random(random, sizeof random);
  for (size_t i = 0; i < sizeof random; ++i) {
    std::snprintf(&slot->token[i * 2], 3, "%02x", random[i]);
  }
  slot->expires = now + kSessionLifetime;
  return std::string(slot->token.data(), 32);
}

std::optional<std::string_view> Auth::cookie_token(std::string_view header) {
  for (size_t pos = 0; pos < header.size();) {
    while (pos < header.size() && (header[pos] == ' ' || header[pos] == ';')) ++pos;
    const size_t end = std::min(header.find(';', pos), header.size());
    const std::string_view pair = header.substr(pos, end - pos);
    if (pair.starts_with("sid=") && pair.size() == 4 + 32) return pair.substr(4);
    pos = end + 1;
  }
  return std::nullopt;
}

bool Auth::authenticated(std::string_view cookie_header, core::Millis now) const {
  const auto token = cookie_token(cookie_header);
  if (!token) return false;
  for (const auto& s : sessions_) {
    if (s.expires > now && constant_time_equal(reinterpret_cast<const uint8_t*>(s.token.data()),
                                               reinterpret_cast<const uint8_t*>(token->data()),
                                               32)) {
      return true;
    }
  }
  return false;
}

void Auth::logout(std::string_view cookie_header) {
  const auto token = cookie_token(cookie_header);
  if (!token) return;
  for (auto& s : sessions_) {
    if (std::string_view(s.token.data(), 32) == *token) s = Session{};
  }
}

std::string Auth::session_cookie(std::string_view token) {
  return "sid=" + std::string(token) + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=604800";
}

}  // namespace device
