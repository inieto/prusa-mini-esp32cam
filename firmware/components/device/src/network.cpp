#include "device/network.hpp"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <nvs.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace device {
namespace {

constexpr const char* kTag = "network";

// Survives software resets (not power loss): lets us tell *why* we rebooted ourselves.
RTC_NOINIT_ATTR uint32_t g_requested_reboot;
constexpr uint32_t kRebootMagic = 0xB7C1A000;

// Without a connection for this long, the setup AP comes up as well (see network.hpp).
constexpr uint64_t kFallbackDelayUs = 3ull * 60 * 1'000'000;
// While the fallback AP is up, the station retries this slowly: every attempt scans all
// channels and drags the AP along, which drops phones connected to it.
constexpr uint64_t kFallbackRetryUs = 30ull * 1'000'000;
// After the station connects, the fallback AP is closed once no client is using it.
constexpr uint64_t kApCloseCheckUs = 30ull * 1'000'000;
// Below this RSSI the ESP32-CAM's PCB antenna struggles to be heard by the router.
constexpr int kWeakRssi = -75;

const char* reason_name(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "auth timeout (router did not answer: weak signal?)";
    case WIFI_REASON_AUTH_FAIL: return "auth failed";
    case WIFI_REASON_ASSOC_EXPIRE: return "association timeout";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "handshake timeout (wrong password?)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout (wrong password?)";
    case WIFI_REASON_NO_AP_FOUND: return "network not found";
    case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout (signal lost)";
    case WIFI_REASON_CONNECTION_FAIL: return "connection failed";
    case WIFI_REASON_ASSOC_LEAVE: return "left";
    default: return "other";
  }
}

esp_timer_handle_t make_timer(esp_timer_cb_t cb, void* arg, const char* name) {
  esp_timer_handle_t t = nullptr;
  const esp_timer_create_args_t args = {.callback = cb, .arg = arg,
                                        .dispatch_method = ESP_TIMER_TASK, .name = name,
                                        .skip_unhandled_events = true};
  ESP_ERROR_CHECK(esp_timer_create(&args, &t));
  return t;
}

}  // namespace

core::Result<void> Network::start(const WifiCredentials& wifi, const std::string& ap_password) {
  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return core::fail(core::Errc::Io, err);

  esp_netif_t* sta = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(sta, wifi.hostname.c_str());
  // Created up front so the fallback AP can be raised later from a timer callback.
  esp_netif_create_default_wifi_ap();  // 192.168.4.1: never collides with 192.168.0.x (B12)
  ap_password_ = ap_password;

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if ((err = esp_wifi_init(&init)) != ESP_OK) return core::fail(core::Errc::Io, err);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Network::on_event, this);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Network::on_event, this);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // credentials live in our NVS namespace

  fallback_timer_ = make_timer(&Network::on_fallback_timer, this, "wifi_fallback");
  retry_timer_ = make_timer(&Network::on_retry_timer, this, "wifi_retry");
  ap_close_timer_ = make_timer(&Network::on_ap_close_timer, this, "ap_close");

  have_sta_ = !wifi.ssid.empty();
  if (have_sta_) {
    wifi_config_t sta_cfg = {};
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.ssid), wifi.ssid.c_str(),
                 sizeof sta_cfg.sta.ssid);
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.password), wifi.password.c_str(),
                 sizeof sta_cfg.sta.password);
    // Scan all channels and pick the strongest AP of the SSID: mesh roaming works (B11).
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_cfg.sta.threshold.authmode = wifi.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.pmf_cfg.capable = true;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    {
      std::lock_guard lock(mutex_);
      ssid_ = wifi.ssid;
    }
    arm_fallback();
  } else {
    start_ap(false);
  }

  if ((err = esp_wifi_start()) != ESP_OK) return core::fail(core::Errc::Io, err);
  esp_wifi_set_ps(WIFI_PS_NONE);  // latency and stability over power: we are mains powered
  set_tx_power(wifi.tx_power_dbm);
  start_services(wifi.hostname);
  return {};
}

// Raises the setup AP "PrusaCam-XXXX". keep_sta: fallback mode, the station keeps retrying.
void Network::start_ap(bool keep_sta) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  wifi_config_t ap_cfg = {};
  const int n = std::snprintf(reinterpret_cast<char*>(ap_cfg.ap.ssid), sizeof ap_cfg.ap.ssid,
                              "PrusaCam-%02X%02X", mac[4], mac[5]);
  ap_cfg.ap.ssid_len = static_cast<uint8_t>(n);
  std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.password), ap_password_.c_str(),
               sizeof ap_cfg.ap.password);
  ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap_cfg.ap.max_connection = 2;
  ap_cfg.ap.channel = 6;
  esp_wifi_set_mode(WIFI_MODE_APSTA);  // STA side kept: to scan networks, or to keep retrying
  esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  ap_active_ = true;
  fallback_ap_ = keep_sta;
  if (keep_sta) {
    ESP_LOGW(kTag, "could not join '%s' for %llu min: setup AP '%s' (password '%s') is up at "
             "http://192.168.4.1; still retrying every %llu s",
             ssid_.c_str(), kFallbackDelayUs / 60'000'000, reinterpret_cast<char*>(ap_cfg.ap.ssid),
             ap_password_.c_str(), kFallbackRetryUs / 1'000'000);
  } else {
    ESP_LOGW(kTag, "No WiFi configured. Setup AP '%s', password '%s', http://192.168.4.1",
             reinterpret_cast<char*>(ap_cfg.ap.ssid), ap_password_.c_str());
  }
}

void Network::arm_fallback() {
  if (!have_sta_ || fallback_ap_) return;
  esp_timer_stop(fallback_timer_);  // restart the countdown
  esp_timer_start_once(fallback_timer_, kFallbackDelayUs);
}

void Network::on_fallback_timer(void* arg) {
  auto* self = static_cast<Network*>(arg);
  if (!self->connected_ && !self->ap_active_) self->start_ap(true);
}

void Network::on_retry_timer(void* arg) {
  auto* self = static_cast<Network*>(arg);
  if (!self->connected_) esp_wifi_connect();
}

void Network::on_ap_close_timer(void* arg) {
  auto* self = static_cast<Network*>(arg);
  if (!self->fallback_ap_ || !self->connected_) return;
  if (self->ap_client_count() > 0) return;  // someone is using it: check again later
  esp_timer_stop(self->ap_close_timer_);
  esp_wifi_set_mode(WIFI_MODE_STA);
  self->ap_active_ = false;
  self->fallback_ap_ = false;
  ESP_LOGI(kTag, "WiFi connected again: setup AP closed");
}

int Network::ap_client_count() const {
  if (!ap_active_) return 0;
  wifi_sta_list_t list{};
  return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

void Network::set_tx_power(uint8_t dbm) {
  // The driver takes quarter-dBm units and rounds to the nearest supported step.
  const esp_err_t err = esp_wifi_set_max_tx_power(static_cast<int8_t>(dbm * 4));
  int8_t applied = 0;
  esp_wifi_get_max_tx_power(&applied);
  ESP_LOGI(kTag, "max TX power %u dBm requested, %.1f dBm applied%s", dbm, applied / 4.0,
           err == ESP_OK ? "" : " (rejected)");
}

void Network::log_disconnect(const wifi_event_sta_disconnected_t& ev) {
  ++failures_;
  // First failure, then every 10th: enough to diagnose without flooding the log.
  if (failures_ != 1 && failures_ % 10 != 0) return;
  // The driver reports -128 when it heard nothing at all (e.g. "network not found").
  char signal[24] = "no signal";
  if (ev.rssi > -128 && ev.rssi != 0) {
    std::snprintf(signal, sizeof signal, "RSSI %d dBm%s", ev.rssi, ev.rssi < kWeakRssi ? " (weak)" : "");
  }
  ESP_LOGW(kTag, "cannot join '%.*s': %s (reason %u), %s, attempt %lu", ev.ssid_len,
           reinterpret_cast<const char*>(ev.ssid), reason_name(ev.reason), ev.reason, signal,
           static_cast<unsigned long>(failures_));
}

void Network::start_services(const std::string& hostname) {
  if (mdns_init() == ESP_OK) {
    mdns_hostname_set(hostname.c_str());
    mdns_instance_name_set("PrusaCam byClaude");
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  }
  esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&sntp);
}

void Network::on_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
  auto* self = static_cast<Network*>(arg);
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (self->have_sta_) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
    if (self->connected_) {
      ESP_LOGW(kTag, "disconnected: %s (reason %u), reconnecting", reason_name(ev->reason),
               ev->reason);
      self->failures_ = 0;
      self->arm_fallback();  // a fresh outage gets the full delay before the AP comes up
    } else {
      self->log_disconnect(*ev);
    }
    self->connected_ = false;
    if (!self->have_sta_) return;
    // The driver paces retries; the recovery ladder handles long outages. With the fallback
    // AP up, retry slowly so phones on the AP are not kicked by channel scans.
    if (self->fallback_ap_) {
      esp_timer_stop(self->retry_timer_);
      esp_timer_start_once(self->retry_timer_, kFallbackRetryUs);
    } else {
      esp_wifi_connect();
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* ev = static_cast<ip_event_got_ip_t*>(data);
    char ip[16];
    std::snprintf(ip, sizeof ip, IPSTR, IP2STR(&ev->ip_info.ip));
    {
      std::lock_guard lock(self->mutex_);
      self->ip_ = ip;
    }
    self->connected_ = true;
    self->failures_ = 0;
    esp_timer_stop(self->fallback_timer_);
    wifi_ap_record_t ap{};
    const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    ESP_LOGI(kTag, "connected, IP %s, RSSI %d dBm", ip, rssi);
    if (rssi != 0 && rssi < kWeakRssi) {
      ESP_LOGW(kTag, "weak WiFi signal (%d dBm): uploads may fail; move the camera closer to "
               "the router or check the antenna", rssi);
    }
    if (self->fallback_ap_) {
      esp_timer_stop(self->ap_close_timer_);
      esp_timer_start_periodic(self->ap_close_timer_, kApCloseCheckUs);
    }
  }
}

core::connect::NetworkSnapshot Network::current() const {
  core::connect::NetworkSnapshot s;
  s.connected = connected_;
  s.mac = mac_string();
  {
    std::lock_guard lock(mutex_);
    s.ipv4 = connected_ ? ip_ : "";
    s.ssid = ssid_;
  }
  wifi_ap_record_t ap{};
  if (connected_ && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s.rssi = ap.rssi;
  return s;
}

core::connect::Mac Network::efuse_mac() const {
  core::connect::Mac mac{};
  esp_efuse_mac_get_default(mac.data());
  return mac;
}

std::string Network::mac_string() const {
  const auto m = efuse_mac();
  char out[18];
  std::snprintf(out, sizeof out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4],
                m[5]);
  return out;
}

void Network::reconnect() {
  ESP_LOGW(kTag, "forcing WiFi reconnection");
  esp_wifi_disconnect();  // STA_DISCONNECTED handler calls esp_wifi_connect()
}

std::vector<ScanResult> Network::scan() {
  std::vector<ScanResult> out;
  if (esp_wifi_scan_start(nullptr, true) != ESP_OK) return out;
  uint16_t count = 20;
  wifi_ap_record_t records[20];
  if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) return out;
  for (uint16_t i = 0; i < count; ++i) {
    const wifi_ap_record_t& r = records[i];
    std::string_view auth = "WPA2";
    switch (r.authmode) {
      case WIFI_AUTH_OPEN: auth = "open"; break;
      case WIFI_AUTH_WEP: auth = "WEP"; break;
      case WIFI_AUTH_WPA_PSK: auth = "WPA"; break;
      case WIFI_AUTH_WPA3_PSK: auth = "WPA3"; break;
      case WIFI_AUTH_WPA2_WPA3_PSK: auth = "WPA2/WPA3"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: auth = "WPA2-Enterprise"; break;
      default: break;
    }
    out.push_back({reinterpret_cast<const char*>(r.ssid), r.rssi, r.primary, auth});
  }
  return out;
}

void EspSystem::reboot(core::RebootReason reason) {
  // Do not pull the rug from under someone reconfiguring the camera on the setup AP; the
  // recovery ladder asks again on its next evaluation.
  if (reason == core::RebootReason::ConnectivityLost && network_.ap_client_count() > 0) {
    // The ladder asks every tick while it is due: log once a minute, not every second.
    static int64_t last_log_us = INT64_MIN / 2;
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_log_us >= 60'000'000) {
      ESP_LOGW(kTag, "connectivity reboot postponed: a client is on the setup AP");
      last_log_us = now_us;
    }
    return;
  }
  g_requested_reboot = kRebootMagic | static_cast<uint32_t>(reason);
  ESP_LOGW(kTag, "rebooting (reason %u)", static_cast<unsigned>(reason));
  esp_restart();
}

std::string_view last_reset_reason() {
  const uint32_t requested = g_requested_reboot;
  g_requested_reboot = 0;
  const esp_reset_reason_t hw = esp_reset_reason();
  if (hw == ESP_RST_SW && (requested & 0xFFFFFF00) == kRebootMagic) {
    switch (static_cast<core::RebootReason>(requested & 0xFF)) {
      case core::RebootReason::Requested: return "requested by user";
      case core::RebootReason::ConnectivityLost: return "connectivity lost";
      case core::RebootReason::OtaApplied: return "firmware update";
      case core::RebootReason::ConfigReset: return "factory reset";
    }
  }
  switch (hw) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_BROWNOUT: return "brownout (supply voltage dropped)";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_EXT: return "reset pin";
    default: return "unknown";
  }
}

std::string setup_ap_password() {
  nvs_handle_t h;
  char stored[24] = {};
  size_t len = sizeof stored;
  if (nvs_open("device", NVS_READWRITE, &h) != ESP_OK) return "prusacam-setup";
  if (nvs_get_str(h, "ap_pass", stored, &len) != ESP_OK) {
    // 10 chars from an unambiguous alphabet, straight from the hardware RNG.
    static constexpr char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    uint8_t random[10];
    esp_fill_random(random, sizeof random);
    for (size_t i = 0; i < sizeof random; ++i) stored[i] = kAlphabet[random[i] % 32];
    stored[sizeof random] = '\0';
    nvs_set_str(h, "ap_pass", stored);
    nvs_commit(h);
  }
  nvs_close(h);
  return stored;
}

}  // namespace device
