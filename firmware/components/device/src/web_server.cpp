#include "device/web_server.hpp"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>

#include "core/settings_json.hpp"
#include "core/storage_policy.hpp"
#include "device/diagnostics.hpp"
#include "device/factory_reset.hpp"
#include "device/firmware.hpp"

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

// Receives exactly out.size() bytes of the body (httpd_req_recv may return less per call).
bool recv_exact(httpd_req_t* req, std::span<uint8_t> out) {
  size_t got = 0;
  int timeouts = 0;
  while (got < out.size()) {
    const int n = httpd_req_recv(req, reinterpret_cast<char*>(out.data() + got), out.size() - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

// ?path=… decoded and sanitised (relative to the SD root); empty optional when invalid.
std::optional<std::string> path_param(httpd_req_t* req) {
  const size_t len = httpd_req_get_url_query_len(req);
  if (len == 0) return std::string();
  if (len > 512) return std::nullopt;
  std::string query(len + 1, '\0');
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return std::nullopt;
  char raw[384];
  if (httpd_query_key_value(query.c_str(), "path", raw, sizeof raw) != ESP_OK) return std::string();
  const auto decoded = core::url_decode(raw);
  if (!decoded) return std::nullopt;
  return core::sanitize_relative_path(*decoded);
}

const char* content_type_for(std::string_view name) {
  if (name.ends_with(".jpg") || name.ends_with(".jpeg")) return "image/jpeg";
  if (name.ends_with(".log") || name.ends_with(".txt")) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

bool is_valid_hostname(std::string_view h) {
  if (h.empty() || h.size() > 63) return false;
  for (char c : h) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) return false;
  }
  return true;
}

void reboot_later(core::SystemControl& system,
                  core::RebootReason reason = core::RebootReason::Requested) {
  // Reply first, reboot from a timer: the handler must return so the response is flushed.
  static core::SystemControl* target = nullptr;
  static core::RebootReason pending = core::RebootReason::Requested;
  static esp_timer_handle_t timer = nullptr;
  target = &system;
  pending = reason;
  if (!timer) {
    const esp_timer_create_args_t args = {
        .callback = [](void*) { target->reboot(pending); },
        .arg = nullptr,
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
  // The ETag must change with the firmware, not only with the UI files: a firmware fix that
  // changes response headers (e.g. Content-Type) would otherwise be masked by 304 replies
  // to a response the browser cached from the old build.
  const auto* app = esp_app_get_description();
  char build[9];
  std::snprintf(build, sizeof build, "%02x%02x%02x%02x", app->app_elf_sha256[0],
                app->app_elf_sha256[1], app->app_elf_sha256[2], app->app_elf_sha256[3]);
  etag_ = "\"" + std::string(d_.bundle.etag()) + "-" + build + "\"";

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

  const std::string& etag = self->etag_;
  if (header(req, "If-None-Match") == etag) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  // httpd_resp_set_type/set_hdr store the pointer, not a copy: the string must outlive
  // httpd_resp_send. A temporary here left a dangling Content-Type (raw HTML shown).
  const std::string content_type(asset->content_type);
  httpd_resp_set_type(req, content_type.c_str());
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
  if (get && route == "stream") return d_.stream.serve(req);
  if (get && route == "logs") {
    const std::string logs = log_snapshot();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, logs.data(), static_cast<ssize_t>(logs.size()));
  }
  if (get && route == "wifi/scan") return wifi_scan(req);
  if ((get || put) && route == "config/storage") return storage_config(req, put);
  if ((get || put) && route == "config/printer") return printer_config(req, put);
  if ((get || put) && route == "config/mqtt") return mqtt_config(req, put);
  if (get && route == "sd") return sd_status(req);
  if (get && route == "sd/list") return sd_list(req);
  if (get && route == "sd/file") return sd_file(req);
  if (post && route == "ota") return ota_upload(req);
  if (get && route == "coredump") return get_coredump(req);
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
    if (route == "preview") {
      d_.service.request_preview();
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
    if (route == "sd/delete") return sd_delete(req, *body);
    if (route == "coredump/erase") {
      erase_coredump();
      return send_json(req, "200 OK", R"({"ok":true})");
    }
    if (route == "factory-reset") {
      erase_user_config();
      reboot_later(d_.system, core::RebootReason::ConfigReset);
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
        return send_error(req, "429 Too Many Requests", "too many attempts, wait 30 s");
      case core::Errc::InvalidArgument:
        return send_error(req, "400 Bad Request", "invalid username or password (at least 8 characters)");
      default:
        return send_error(req, "401 Unauthorized", setup ? "initial setup is closed"
                                                         : "wrong username or password");
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
  cJSON_AddBoolToObject(c, "uploads_paused", s.uploads_paused);
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

  const PrinterSettings ps = d_.printer.settings();
  if (ps.host.empty()) {
    cJSON_AddNullToObject(root.get(), "printer");
  } else {
    const core::PrinterJob job = d_.printer.job();
    cJSON* p = cJSON_AddObjectToObject(root.get(), "printer");
    cJSON_AddBoolToObject(p, "reachable", d_.printer.reachable());
    cJSON_AddStringToObject(p, "error", d_.printer.last_error().c_str());
    cJSON_AddStringToObject(p, "phase", std::string(core::to_string(job.phase)).c_str());
    cJSON_AddStringToObject(p, "file", job.file.c_str());
    cJSON_AddNumberToObject(p, "progress", job.progress);
    cJSON_AddNumberToObject(p, "time_left_s", job.time_left_s);
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

  const FirmwareStatus fw = firmware_status();
  cJSON* f = cJSON_AddObjectToObject(root.get(), "firmware");
  cJSON_AddStringToObject(f, "partition", fw.running_partition.c_str());
  cJSON_AddBoolToObject(f, "pending_verify", fw.pending_verify);
  if (fw.rolled_back_version) cJSON_AddStringToObject(f, "rolled_back", fw.rolled_back_version->c_str());
  else cJSON_AddNullToObject(f, "rolled_back");

  if (const auto dump = coredump_info()) {
    cJSON* cd = cJSON_AddObjectToObject(root.get(), "coredump");
    cJSON_AddNumberToObject(cd, "size", static_cast<double>(dump->size));
    cJSON_AddStringToObject(cd, "task", dump->task.c_str());
    char pc[11];
    std::snprintf(pc, sizeof pc, "0x%08lx", static_cast<unsigned long>(dump->pc));
    cJSON_AddStringToObject(cd, "pc", pc);
    cJSON_AddStringToObject(cd, "reason", dump->reason.c_str());
  } else {
    cJSON_AddNullToObject(root.get(), "coredump");
  }

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
    if (!merged) return send_error(req, "400 Bad Request", "invalid value");
    if (!d_.camera.supports(merged->resolution)) {
      return send_error(req, "400 Bad Request", "resolution not supported by this sensor");
    }
    settings = *merged;
    if (auto r = d_.store.save_camera(settings); !r) {
      return send_error(req, "500 Internal Server Error", "could not save");
    }
    d_.camera.request_settings(settings);  // applied by the capture task before its next frame
  }
  // Read-only facts about the sensor ride along so the UI can offer only valid resolutions.
  const std::string json = core::camera_settings_to_json(settings);
  JsonPtr j(cJSON_ParseWithLength(json.data(), json.size()));
  cJSON_AddStringToObject(j.get(), "sensor", d_.camera.sensor_name().c_str());
  cJSON_AddStringToObject(j.get(), "max_resolution",
                          std::string(core::resolution_name(d_.camera.max_resolution())).c_str());
  cJSON_AddNumberToObject(j.get(), "max_zoom_pct", d_.camera.max_zoom_pct(settings.resolution));
  return send_json(req, "200 OK", print(j.get()));
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
      return send_error(req, "400 Bad Request", "the token must be 20 alphanumeric characters");
    }
    if (has_host && !is_valid_hostname(host)) return send_error(req, "400 Bad Request", "invalid server");
    if (has_token) creds.token = token;
    if (has_host) creds.host = host;
    if (auto r = d_.store.save_connect(creds); !r) {
      return send_error(req, "500 Internal Server Error", "could not save");
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
  bool rebooting = false;
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
    if (!j) return send_error(req, "400 Bad Request", "invalid JSON");
    bool has_ssid = false, has_pass = false, has_host = false;
    const std::string ssid = json_string(j.get(), "ssid", &has_ssid);
    const std::string pass = json_string(j.get(), "password", &has_pass);
    const std::string host = json_string(j.get(), "hostname", &has_host);
    if (has_ssid && (ssid.empty() || ssid.size() > 32)) return send_error(req, "400 Bad Request", "invalid SSID");
    if (has_pass && !pass.empty() && (pass.size() < 8 || pass.size() > 63)) {
      return send_error(req, "400 Bad Request", "the WiFi password must be 8 to 63 characters");
    }
    if (has_host && (!is_valid_hostname(host) || host.find('.') != std::string::npos)) {
      return send_error(req, "400 Bad Request", "invalid name (letters, digits and hyphens)");
    }
    const cJSON* tx = cJSON_GetObjectItemCaseSensitive(j.get(), "tx_power_dbm");
    if (tx && (!cJSON_IsNumber(tx) || !valid_tx_power(tx->valueint))) {
      return send_error(req, "400 Bad Request", "transmit power must be 8 to 20 dBm");
    }
    if (has_ssid) wifi.ssid = ssid;
    if (has_pass) wifi.password = pass;
    if (has_host) wifi.hostname = host;
    if (tx) wifi.tx_power_dbm = static_cast<uint8_t>(tx->valueint);
    if (auto r = d_.store.save_wifi(wifi); !r) return send_error(req, "500 Internal Server Error", "could not save");
    if (tx) d_.network.set_tx_power(wifi.tx_power_dbm);  // live, nothing to reboot for
    reboot_required = has_ssid || has_pass || has_host;
    // Without a working station (first setup or fallback AP) there is nothing to keep running:
    // reboot straight away so the camera joins the new network without another click.
    if (reboot_required && !d_.network.connected() && !wifi.ssid.empty()) {
      reboot_later(d_.system);
      rebooting = true;
    }
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "ssid", wifi.ssid.c_str());
  cJSON_AddBoolToObject(j.get(), "password_set", !wifi.password.empty());
  cJSON_AddStringToObject(j.get(), "hostname", wifi.hostname.c_str());
  cJSON_AddNumberToObject(j.get(), "tx_power_dbm", wifi.tx_power_dbm);
  cJSON_AddBoolToObject(j.get(), "reboot_required", reboot_required);
  cJSON_AddBoolToObject(j.get(), "rebooting", rebooting);
  return send_json(req, "200 OK", print(j.get()));
}

// Raw image body (application/octet-stream: like JSON, it forces a CORS preflight, so together
// with SameSite=Strict no other site can push firmware through a logged-in browser).
esp_err_t WebServer::ota_upload(httpd_req_t* req) {
  if (header(req, "Content-Type") != "application/octet-stream") {
    return send_error(req, "415 Unsupported Media Type", "send the .bin as application/octet-stream");
  }
  OtaWriter ota;
  if (auto r = ota.begin(req->content_len); !r) {
    return r.error().code == core::Errc::OutOfRange
               ? send_error(req, "400 Bad Request", "file size does not fit a firmware image")
               : send_error(req, "500 Internal Server Error", "cannot start the update");
  }
  constexpr size_t kChunk = 4096;
  static_assert(kChunk >= OtaWriter::kHeaderBytes);
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kChunk]);
  if (!buffer) return send_error(req, "500 Internal Server Error", "out of memory");

  for (size_t received = 0; received < req->content_len;) {
    const size_t n = std::min(kChunk, req->content_len - received);
    if (!recv_exact(req, {buffer.get(), n})) {
      return send_error(req, "400 Bad Request", "upload interrupted");
    }
    if (auto r = ota.write({buffer.get(), n}); !r) {
      switch (r.error().code) {
        case core::Errc::Unsupported:
          return send_error(req, "400 Bad Request", "this is not a PrusaCam byClaude firmware");
        case core::Errc::InvalidArgument:
          return send_error(req, "400 Bad Request", "not an ESP32 firmware image");
        default:
          return send_error(req, "500 Internal Server Error", "flash write failed");
      }
    }
    received += n;
  }
  const core::Result<std::string> version = ota.finish();
  if (!version) {
    return send_error(req, "400 Bad Request", "image verification failed (corrupt or truncated)");
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddBoolToObject(j.get(), "ok", true);
  cJSON_AddStringToObject(j.get(), "version", version->c_str());
  reboot_later(d_.system, core::RebootReason::OtaApplied);
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::get_coredump(httpd_req_t* req) {
  const auto dump = coredump_info();
  if (!dump) return send_error(req, "404 Not Found", "no core dump stored");
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  // Must outlive the response (httpd keeps the pointer).
  const std::string disposition = "attachment; filename=\"coredump-" + d_.version + ".elf\"";
  httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
  constexpr size_t kChunk = 4096;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kChunk]);
  if (!buffer) return send_error(req, "500 Internal Server Error", "out of memory");
  for (size_t offset = 0; offset < dump->size;) {
    const size_t n = std::min(kChunk, dump->size - offset);
    if (!read_coredump(offset, {buffer.get(), n})) break;
    if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(buffer.get()),
                              static_cast<ssize_t>(n)) != ESP_OK) {
      return ESP_FAIL;
    }
    offset += n;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t WebServer::storage_config(httpd_req_t* req, bool update) {
  core::StorageSettings s = d_.storage.settings();
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
    if (!j || !cJSON_IsObject(j.get())) return send_error(req, "400 Bad Request", "invalid JSON");
    int known = 0;
    if (const cJSON* v = cJSON_GetObjectItemCaseSensitive(j.get(), "log_to_sd")) {
      if (!cJSON_IsBool(v)) return send_error(req, "400 Bad Request", "invalid value");
      s.log_to_sd = cJSON_IsTrue(v);
      ++known;
    }
    if (const cJSON* v = cJSON_GetObjectItemCaseSensitive(j.get(), "timelapse")) {
      const auto mode = cJSON_IsString(v) ? core::timelapse_mode_from(v->valuestring) : std::nullopt;
      if (!mode) return send_error(req, "400 Bad Request", "invalid timelapse mode");
      s.timelapse = *mode;
      ++known;
    }
    if (const cJSON* v = cJSON_GetObjectItemCaseSensitive(j.get(), "timelapse_interval_s")) {
      if (!cJSON_IsNumber(v) || v->valuedouble < 10 || v->valuedouble > 3600) {
        return send_error(req, "400 Bad Request", "interval must be 10 to 3600 s");
      }
      s.timelapse_interval_s = static_cast<uint16_t>(v->valueint);
      ++known;
    }
    if (known != cJSON_GetArraySize(j.get())) return send_error(req, "400 Bad Request", "unknown field");
    if (auto r = d_.store.save_storage(s); !r) {
      return send_error(req, "500 Internal Server Error", "could not save");
    }
    d_.storage.set_settings(s);
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddBoolToObject(j.get(), "log_to_sd", s.log_to_sd);
  cJSON_AddStringToObject(j.get(), "timelapse", std::string(core::to_string(s.timelapse)).c_str());
  cJSON_AddNumberToObject(j.get(), "timelapse_interval_s", s.timelapse_interval_s);
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::printer_config(httpd_req_t* req, bool update) {
  PrinterSettings p = d_.printer.settings();
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
    if (!j || !cJSON_IsObject(j.get())) return send_error(req, "400 Bad Request", "invalid JSON");
    bool has_host = false, has_user = false, has_pass = false;
    const std::string host = json_string(j.get(), "host", &has_host);
    const std::string user = json_string(j.get(), "user", &has_user);
    const std::string pass = json_string(j.get(), "password", &has_pass);
    if (has_host && !host.empty() && !PrinterMonitor::valid_host(host)) {
      return send_error(req, "400 Bad Request", "invalid printer address");
    }
    if (has_user && (user.empty() || user.size() > 32)) return send_error(req, "400 Bad Request", "invalid user");
    if (has_pass && pass.size() > 64) return send_error(req, "400 Bad Request", "password too long");
    if (has_host) p.host = host;
    if (has_user) p.user = user;
    if (has_pass) p.password = pass;
    if (const cJSON* v = cJSON_GetObjectItemCaseSensitive(j.get(), "upload_only_while_printing")) {
      if (!cJSON_IsBool(v)) return send_error(req, "400 Bad Request", "invalid value");
      p.upload_only_while_printing = cJSON_IsTrue(v);
    }
    if (auto r = d_.store.save_printer(p); !r) {
      return send_error(req, "500 Internal Server Error", "could not save");
    }
    d_.printer.set_settings(p);
    d_.service.set_upload_only_while_printing(p.upload_only_while_printing);
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "host", p.host.c_str());
  cJSON_AddStringToObject(j.get(), "user", p.user.c_str());
  cJSON_AddBoolToObject(j.get(), "password_set", !p.password.empty());  // never echo it
  cJSON_AddBoolToObject(j.get(), "upload_only_while_printing", p.upload_only_while_printing);
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::mqtt_config(httpd_req_t* req, bool update) {
  MqttSettings m = d_.mqtt.settings();
  if (update) {
    const auto body = read_json_body(req);
    if (!body) return send_error(req, "415 Unsupported Media Type", "JSON body required");
    JsonPtr j(cJSON_ParseWithLength(body->data(), body->size()));
    if (!j || !cJSON_IsObject(j.get())) return send_error(req, "400 Bad Request", "invalid JSON");
    bool has_uri = false, has_user = false, has_pass = false;
    const std::string uri = json_string(j.get(), "uri", &has_uri);
    const std::string user = json_string(j.get(), "user", &has_user);
    const std::string pass = json_string(j.get(), "password", &has_pass);
    if (has_uri && !uri.empty() && !MqttBridge::valid_uri(uri)) {
      return send_error(req, "400 Bad Request", "broker must look like mqtt://host:1883 (no TLS)");
    }
    if ((has_user && user.size() > 64) || (has_pass && pass.size() > 64)) {
      return send_error(req, "400 Bad Request", "user or password too long");
    }
    if (has_uri) m.uri = uri;
    if (has_user) m.user = user;
    if (has_pass) m.password = pass;
    if (const cJSON* v = cJSON_GetObjectItemCaseSensitive(j.get(), "publish_images")) {
      if (!cJSON_IsBool(v)) return send_error(req, "400 Bad Request", "invalid value");
      m.publish_images = cJSON_IsTrue(v);
    }
    if (auto r = d_.store.save_mqtt(m); !r) return send_error(req, "500 Internal Server Error", "could not save");
    d_.mqtt.set_settings(m);
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "uri", m.uri.c_str());
  cJSON_AddStringToObject(j.get(), "user", m.user.c_str());
  cJSON_AddBoolToObject(j.get(), "password_set", !m.password.empty());
  cJSON_AddBoolToObject(j.get(), "publish_images", m.publish_images);
  cJSON_AddBoolToObject(j.get(), "connected", d_.mqtt.connected());
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::sd_status(httpd_req_t* req) {
  const SdUsage u = d_.storage.usage();
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddBoolToObject(j.get(), "mounted", u.mounted);
  cJSON_AddNumberToObject(j.get(), "total_bytes", static_cast<double>(u.total_bytes));
  cJSON_AddNumberToObject(j.get(), "free_bytes", static_cast<double>(u.free_bytes));
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::sd_list(httpd_req_t* req) {
  const auto path = path_param(req);
  if (!path) return send_error(req, "400 Bad Request", "invalid path");
  const auto entries = d_.storage.list(*path);
  if (!entries) {
    return entries.error().code == core::Errc::NotFound
               ? send_error(req, "404 Not Found", "no such folder")
               : send_error(req, "409 Conflict", "no SD card");
  }
  JsonPtr j(cJSON_CreateObject());
  cJSON_AddStringToObject(j.get(), "path", path->c_str());
  cJSON* list = cJSON_AddArrayToObject(j.get(), "entries");
  for (const SdEntry& e : *entries) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", e.name.c_str());
    cJSON_AddBoolToObject(item, "dir", e.dir);
    cJSON_AddNumberToObject(item, "size", e.size);
    cJSON_AddItemToArray(list, item);
  }
  return send_json(req, "200 OK", print(j.get()));
}

esp_err_t WebServer::sd_file(httpd_req_t* req) {
  const auto path = path_param(req);
  if (!path || path->empty()) return send_error(req, "400 Bad Request", "invalid path");
  if (!d_.storage.usage().mounted) return send_error(req, "409 Conflict", "no SD card");
  FILE* f = std::fopen(Storage::absolute(*path).c_str(), "rb");
  if (!f) return send_error(req, "404 Not Found", "no such file");
  const std::string name = path->substr(path->rfind('/') == std::string::npos ? 0 : path->rfind('/') + 1);
  const char* type = content_type_for(name);
  httpd_resp_set_type(req, type);  // string literal: outlives the response
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  // Only images open inline; anything else downloads (no HTML can be served from the card).
  const std::string disposition =
      std::string(std::string_view(type).starts_with("image/") ? "inline" : "attachment") +
      "; filename=\"" + name + "\"";
  httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
  constexpr size_t kChunk = 4096;
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[kChunk]);
  esp_err_t err = buffer ? ESP_OK : ESP_ERR_NO_MEM;
  while (err == ESP_OK) {
    const size_t n = std::fread(buffer.get(), 1, kChunk, f);
    if (n == 0) break;
    err = httpd_resp_send_chunk(req, buffer.get(), static_cast<ssize_t>(n));
  }
  std::fclose(f);
  if (err != ESP_OK) return ESP_FAIL;
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t WebServer::sd_delete(httpd_req_t* req, std::string_view body) {
  JsonPtr j(cJSON_ParseWithLength(body.data(), body.size()));
  const std::string raw = j ? json_string(j.get(), "path") : "";
  const auto path = core::sanitize_relative_path(raw);
  if (!path || path->empty()) return send_error(req, "400 Bad Request", "invalid path");
  if (auto r = d_.storage.remove(*path); !r) {
    return r.error().code == core::Errc::NotFound ? send_error(req, "404 Not Found", "no such file")
                                                   : send_error(req, "409 Conflict", "no SD card");
  }
  return send_json(req, "200 OK", R"({"ok":true})");
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
