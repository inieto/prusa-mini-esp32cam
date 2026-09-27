#include "device/web_server.hpp"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <memory>

#include "core/settings_json.hpp"
#include "device/diagnostics.hpp"

namespace device {
namespace {

constexpr const char* kTag = "web";
constexpr size_t kMaxBody = 2048;
constexpr std::string_view kApiPrefix = "/api/v1/";

struct JsonDeleter {
  void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string print(const cJSON* json) {
  char* text = cJSON_PrintUnformatted(json);
  std::string out = text ? text : "{}";
  cJSON_free(text);
  return out;
}

esp_err_t send_json(httpd_req_t* req, const char* status, std::string_view body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t send_error(httpd_req_t* req, const char* status, const char* message) {
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "error", message);
  return send_json(req, status, print(j.get()));
}

std::string header(httpd_req_t* req, const char* name) {
  const size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0 || len > 1024) return {};
  std::string value(len + 1, '\0');
  httpd_req_get_hdr_value_str(req, name, value.data(), value.size());
  value.resize(len);
  return value;
}

// Reads a JSON request body. Requiring application/json forces a CORS preflight for
// cross-site requests, which we never answer: together with SameSite=Strict, that stops CSRF.
std::optional<std::string> read_json_body(httpd_req_t* req) {
  if (!header(req, "Content-Type").starts_with("application/json")) return std::nullopt;
  if (req->content_len > kMaxBody) return std::nullopt;
  std::string body(req->content_len, '\0');
  size_t received = 0;
  while (received < body.size()) {
    const int n = httpd_req_recv(req, body.data() + received, body.size() - received);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return std::nullopt;
    received += static_cast<size_t>(n);
  }
  return body;
}

std::string json_string(const cJSON* obj, const char* key, bool* present = nullptr) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (present) *present = cJSON_IsString(item);
  return cJSON_IsString(item) ? item->valuestring : "";
}

bool is_valid_hostname(std::string_view h) {
  if (h.empty() || h.size() > 63) return false;
  for (char c : h) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) return false;
  }
  return true;
}

void reboot_later(core::SystemControl& system) {
  // Reply first, reboot from a timer: the handler must return so the response is flushed.
  static esp_timer_handle_t timer = nullptr;
  if (!timer) {
    const esp_timer_create_args_t args = {
        .callback = [](void* s) {
          static_cast<core::SystemControl*>(s)->reboot(core::RebootReason::Requested);
        },
        .arg = &system,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "reboot",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &timer);
  }
  esp_timer_start_once(timer, 500'000);
}

}  // namespace

extern "C" const uint8_t _binary_web_bundle_bin_start[];
extern "C" const uint8_t _binary_web_bundle_bin_end[];

std::span<const uint8_t> embedded_web_bundle() {
  return {_binary_web_bundle_bin_start, _binary_web_bundle_bin_end};
}

core::Result<void> WebServer::start() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 8;
  config.max_open_sockets = 6;
  config.lru_purge_enable = true;
  config.stack_size = 8192;
  config.core_id = 1;
  const esp_err_t err = httpd_start(&server_, &config);
  if (err != ESP_OK) return core::fail(core::Errc::Io, err);

  for (httpd_method_t method : {HTTP_GET, HTTP_POST, HTTP_PUT}) {
    const httpd_uri_t api = {.uri = "/api/v1/*", .method = method, .handler = on_api,
                             .user_ctx = this};
    httpd_register_uri_handler(server_, &api);
  }
  const httpd_uri_t assets = {.uri = "/*", .method = HTTP_GET, .handler = on_static,
                              .user_ctx = this};
  httpd_register_uri_handler(server_, &assets);
  ESP_LOGI(kTag, "HTTP server on port 80 (%u UI assets)", static_cast<unsigned>(d_.bundle.size()));
  return {};
}

// ---------- static UI ----------
esp_err_t WebServer::on_static(httpd_req_t* req) {
  auto* self = static_cast<WebServer*>(req->user_ctx);
  std::string_view path(req->uri);
  path = path.substr(0, path.find('?'));
  if (path == "/") path = "/index.html";
  const auto asset = self->d_.bundle.find(path.substr(1));
  if (!asset) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_send(req, "Not found", HTTPD_RESP_USE_STRLEN);
  }

  const std::string etag = "\"" + std::string(self->d_.bundle.etag()) + "\"";
  if (header(req, "If-None-Match") == etag) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, std::string(asset->content_type).c_str());
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "ETag", etag.c_str());
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");  // revalidate: updates show at once
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Content-Security-Policy",
                     "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
                     "frame-ancestors 'none'");
  return httpd_resp_send(req, reinterpret_cast<const char*>(asset->gzip.data()),
                         static_cast<ssize_t>(asset->gzip.size()));
}

// ---------- API ----------
esp_err_t WebServer::on_api(httpd_req_t* req) {
  auto* self = static_cast<WebServer*>(req->user_ctx);
  std::string_view route(req->uri);
  route = route.substr(kApiPrefix.size());
  route = route.substr(0, route.find('?'));
  return self->handle_api(req, route);
}

bool WebServer::is_authenticated(httpd_req_t* req) const {
  return d_.auth.authenticated(header(req, "Cookie"), d_.clock.now());
}

esp_err_t WebServer::handle_api(httpd_req_t* req, std::string_view route) {
  const bool get = req->method == HTTP_GET;
  const bool post = req->method == HTTP_POST;
  const bool put = req->method == HTTP_PUT;

  // Public endpoints.
  if (get && route == "session") {
    JsonPtr j(cJSON_CreateObject());
    cJSON_AddBoolToObject(j.get(), "authenticated", is_authenticated(req));
    cJSON_AddBoolToObject(j.get(), "setup_required", d_.auth.setup_required());
    cJSON_AddBoolToObject(j.get(), "setup_window_open",
                          d_.auth.setup_window_open(d_.clock.now()));
    return send_json(req, "200 OK", print(j.get()));
  }
  if (post && route == "login") return login_or_setup(req, false);
  if (post && route == "setup") return login_or_setup(req, true);

  if (!is_authenticated(req)) return send_error(req, "401 Unauthorized", "login required");

  if (post && route == "logout") {
    d_.auth.logout(header(req, "Cookie"));
    httpd_resp_set_hdr(req, "Set-Cookie", Auth::kClearCookie.data());
    return send_json(req, "200 OK", R"({"ok":true})");
  }
  if (get && route == "status") return get_status(req);
  if (get && route == "snapshot.jpg") return get_snapshot(req);
  if (get && route == "logs") {
    const std::string logs = log_snapshot();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, logs.data(), static_cast<ssize_t>(logs.size()));
  }
  if (get && route == "wifi/scan") return wifi_scan(req);
  if ((get || put) && route == "config/camera") return camera_config(req, put);
  if ((get || put) && route == "config/connect") return connect_config(req, put);
  if ((get || put) && route == "config/network") return network_config(req, put);

  if (post) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    if (route == "snapshot") {
      d_.service.request_snapshot();
      return send_json(req, "202 Accepted", R"({"ok":true})");
    }
    if (route == "light") {
      JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
      d_.camera.set_light(j && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j.get(), "on")));
      return send_json(req, "200 OK", R"({"ok":true})");
    }
    if (route == "reboot") {
      reboot_later(d_.system);
      return send_json(req, "202 Accepted", R"({"ok":true})");
    }
  }
  return send_error(req, "404 Not Found", "unknown endpoint");
}

esp_err_t WebServer::login_or_setup(httpd_req_t* req, bool setup) {
  const auto body = read_json_body(req);
  if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
  JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
  if (!j) return send_error(req, "400 Bad Request", "invalid JSON");
  const std::string user = json_string(j.get(), "username");
  const std::string pass = json_string(j.get(), "password");
  const core::Millis now = d_.clock.now();

  const core::Result<std::string> session =
      setup ? d_.auth.setup(user, pass, now) : d_.auth.login(user, pass, now);
  if (!session) {
    switch (session.error().code) {
      case core::Errc::Busy:
        return send_error(req, "429 Too Many Requests", "demasiados intentos, esperá 30 s");
      case core::Errc::InvalidArgument:
        return send_error(req, "400 Bad Request", "usuario o contraseña inválidos (mínimo 8 caracteres)");
      default:
        return send_error(req, "401 Unauthorized", setup ? "configuración inicial cerrada"
                                                         : "usuario o contraseña incorrectos");
    }
  }
  const std::string cookie = Auth::session_cookie(*session);
  httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
  return send_json(req, "200 OK", R"({"ok":true})");
}

esp_err_t WebServer::get_status(httpd_req_t* req) {
  const core::Millis now = d_.clock.now();
  const core::ServiceStatus s = d_.service.status();
  const core::FrameRef frame = d_.service.latest_frame();
  const core::connect::NetworkSnapshot net = d_.network.current();

  JsonPtr root(cJSON_CreateObject());
  cJSON_AddStringToObject(root.get(), "version", d_.version.c_str());
  cJSON_AddNumberToObject(root.get(), "uptime_s", static_cast<double>(now.count() / 1000));
  cJSON_AddBoolToObject(root.get(), "light", d_.camera.light());

  cJSON* c = cJSON_AddObjectToObject(root.get(), "connect");
  cJSON_AddStringToObject(c, "state", std::string(core::to_string(s.state)).c_str());
  if (s.interval) cJSON_AddNumberToObject(c, "interval_s", static_cast<double>(s.interval->count() / 1000));
  else cJSON_AddNullToObject(c, "interval_s");
  if (s.last_upload_ok_at) {
    cJSON_AddNumberToObject(c, "last_upload_ago_s",
                            static_cast<double>((now - *s.last_upload_ok_at).count() / 1000));
  } else {
    cJSON_AddNullToObject(c, "last_upload_ago_s");
  }
  cJSON_AddNumberToObject(c, "uploads_ok", s.uploads_ok);
  cJSON_AddNumberToObject(c, "uploads_failed", s.uploads_failed);
  cJSON_AddNumberToObject(c, "capture_failures", s.capture_failures);
  if (s.last_error) {
    char text[64];
    std::snprintf(text, sizeof text, "%s (%ld)", std::string(core::to_string(s.last_error->code)).c_str(),
                  static_cast<long>(s.last_error->detail));
    cJSON_AddStringToObject(c, "last_error", text);
  } else {
    cJSON_AddNullToObject(c, "last_error");
  }
  if (frame) {
    cJSON_AddNumberToObject(c, "frame_seq", frame.info().sequence);
    cJSON_AddNumberToObject(c, "frame_age_s",
                            static_cast<double>((now - frame.info().captured_at).count() / 1000));
  }

  cJSON* w = cJSON_AddObjectToObject(root.get(), "wifi");
  cJSON_AddStringToObject(w, "ssid", net.ssid.c_str());
  cJSON_AddStringToObject(w, "ip", net.ipv4.c_str());
  cJSON_AddNumberToObject(w, "rssi", net.rssi);
  cJSON_AddBoolToObject(w, "setup_ap", d_.network.setup_ap_active());

  cJSON* h = cJSON_AddObjectToObject(root.get(), "heap");
  cJSON_AddNumberToObject(h, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  cJSON_AddNumberToObject(h, "internal_min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  cJSON_AddNumberToObject(h, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  cJSON* resets = cJSON_AddArrayToObject(root.get(), "resets");
  for (const ResetRecord& r : reset_history()) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "reason", r.reason.c_str());
    cJSON_AddNumberToObject(item, "uptime_s", r.uptime_s);
    cJSON_AddItemToArray(resets, item);
  }
  return send_json(req, "200 OK", print(root.get()));
}

esp_err_t WebServer::get_snapshot(httpd_req_t* req) {
  // Holding the FrameRef keeps the slot alive for the whole send (ADR-0003).
  const core::FrameRef frame = d_.service.latest_frame();
  if (!frame) return send_error(req, "404 Not Found", "no snapshot yet");
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const auto bytes = frame.bytes();
  return httpd_resp_send(req, reinterpret_cast<const char*>(bytes.data()),
                         static_cast<ssize_t>(bytes.size()));
}

esp_err_t WebServer::camera_config(httpd_req_t* req, bool update) {
  core::CameraSettings settings = d_.camera.settings();
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    const auto merged = core::merge_camera_settings(settings, *body);
    if (!merged) return send_error(req, "400 Bad Request", "valor inválido");
    settings = *merged;
    if (auto r = d_.store.save_camera(settings); !r) {
      return send_error(req, "500 Internal Server Error", "no se pudo guardar");
    }
    d_.camera.request_settings(settings);  // applied by the capture task before its next frame
  }
  return send_json(req, "200 OK", core::camera_settings_to_json(settings));
}

esp_err_t WebServer::connect_config(httpd_req_t* req, bool update) {
  ConnectCredentials creds = d_.connect.credentials();
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
    if (!j) return send_error(req, "400 Bad Request", "invalid JSON");
    bool has_token = false, has_host = false;
    const std::string token = json_string(j.get(), "token", &has_token);
    const std::string host = json_string(j.get(), "hostname", &has_host);
    if (has_token && !token.empty() && !core::connect::is_valid_token(token)) {
      return send_error(req, "400 Bad Request", "el token tiene que tener 20 caracteres alfanuméricos");
    }
    if (has_host && !is_valid_hostname(host)) return send_error(req, "400 Bad Request", "servidor inválido");
    if (has_token) creds.token = token;
    if (has_host) creds.host = host;
    if (auto r = d_.store.save_connect(creds); !r) {
      return send_error(req, "500 Internal Server Error", "no se pudo guardar");
    }
    d_.connect.set_credentials(creds);
    d_.service.request_info_sync();
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "hostname", creds.host.c_str());
  cJSON_AddStringToObject(j.get(), "fingerprint", creds.fingerprint.c_str());
  cJSON_AddBoolToObject(j.get(), "token_set", !creds.token.empty());  // never echo the token
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::network_config(httpd_req_t* req, bool update) {
  WifiCredentials wifi = d_.store.load_wifi();
  bool reboot_required = false;
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
    if (!j) return send_error(req, "400 Bad Request", "invalid JSON");
    bool has_ssid = false, has_pass = false, has_host = false;
    const std::string ssid = json_string(j.get(), "ssid", &has_ssid);
    const std::string pass = json_string(j.get(), "password", &has_pass);
    const std::string host = json_string(j.get(), "hostname", &has_host);
    if (has_ssid && (ssid.empty() || ssid.size() > 32)) return send_error(req, "400 Bad Request", "SSID inválido");
    if (has_pass && !pass.empty() && (pass.size() < 8 || pass.size() > 63)) {
      return send_error(req, "400 Bad Request", "la clave WiFi tiene que tener entre 8 y 63 caracteres");
    }
    if (has_host && (!is_valid_hostname(host) || host.find('.') != std::string::npos)) {
      return send_error(req, "400 Bad Request", "nombre inválido (letras, números y guiones)");
    }
    if (has_ssid) wifi.ssid = ssid;
    if (has_pass) wifi.password = pass;
    if (has_host) wifi.hostname = host;
    if (auto r = d_.store.save_wifi(wifi); !r) return send_error(req, "500 Internal Server Error", "no se pudo guardar");
    reboot_required = true;
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "ssid", wifi.ssid.c_str());
  cJSON_AddBoolToObject(j.get(), "password_set", !wifi.password.empty());
  cJSON_AddStringToObject(j.get(), "hostname", wifi.hostname.c_str());
  cJSON_AddBoolToObject(j.get(), "reboot_required", reboot_required);
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::wifi_scan(httpd_req_t* req) {
  JsonPtr list(cJSON_CreateArray());
  for (const ScanResult& n : d_.network.scan()) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "ssid", n.ssid.c_str());
    cJSON_AddNumberToObject(item, "rssi", n.rssi);
    cJSON_AddNumberToObject(item, "channel", n.channel);
    cJSON_AddStringToObject(item, "auth", std::string(n.auth).c_str());
    cJSON_AddItemToArray(list.get(), item);
  }
  return send_json(req, "200 OK", print(list.get()));
}

}  // namespace device
