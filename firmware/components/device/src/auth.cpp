#include "device/auth.hpp"

#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
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

std::array<uint8_t, 32> sha256(std::string_view text) {
  std::array<uint8_t, 32> out{};
  mbedtls_sha256(reinterpret_cast<const unsigned char*>(text.data()), text.size(), out.data(), 0);
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
  size_t api_len = api_hash_.size();
  api_token_set_ = nvs_get_blob(h, "api_hash", api_hash_.data(), &api_len) == ESP_OK &&
                   api_len == api_hash_.size();
  nvs_close(h);
}

core::Result<std::string> Auth::generate_api_token() {
  uint8_t random[16];
  esp_fill_random(random, sizeof random);  // hardware RNG (ADR-0005)
  char token[33];
  for (size_t i = 0; i < sizeof random; ++i) std::snprintf(&token[i * 2], 3, "%02x", random[i]);
  const std::array<uint8_t, 32> hash = sha256(token);
  nvs_handle_t h;
  if (nvs_open("web", NVS_READWRITE, &h) != ESP_OK) return core::fail(core::Errc::Io);
  nvs_set_blob(h, "api_hash", hash.data(), hash.size());
  const esp_err_t err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) return core::fail(core::Errc::Io, err);
  api_hash_ = hash;
  api_token_set_ = true;
  ESP_LOGI(kTag, "new API token generated (the previous one, if any, no longer works)");
  return std::string(token);
}

void Auth::revoke_api_token() {
  nvs_handle_t h;
  if (nvs_open("web", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_key(h, "api_hash");
    nvs_commit(h);
    nvs_close(h);
  }
  api_token_set_ = false;
  api_hash_ = {};
  ESP_LOGI(kTag, "API token revoked");
}

bool Auth::api_token_valid(std::string_view token) const {
  if (!api_token_set_ || token.size() != 32) return false;
  const std::array<uint8_t, 32> hash = sha256(token);
  return constant_time_equal(hash.data(), api_hash_.data(), hash.size());
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
