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

#include <cstdio>
#include <cstring>

namespace device {
namespace {

constexpr const char* kTag = "network";

// Survives software resets (not power loss): lets us tell *why* we rebooted ourselves.
RTC_NOINIT_ATTR uint32_t g_requested_reboot;
constexpr uint32_t kRebootMagic = 0xB7C1A000;

}  // namespace

core::Result<void> Network::start(const WifiCredentials& wifi, const std::string& ap_password) {
  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return core::fail(core::Errc::Io, err);

  esp_netif_t* sta = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(sta, wifi.hostname.c_str());

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if ((err = esp_wifi_init(&init)) != ESP_OK) return core::fail(core::Errc::Io, err);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Network::on_event, this);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Network::on_event, this);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // credentials live in our NVS namespace

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
    std::lock_guard lock(mutex_);
    ssid_ = wifi.ssid;
  } else {
    esp_netif_create_default_wifi_ap();  // 192.168.4.1: never collides with 192.168.0.x (B12)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap_cfg = {};
    const int n = std::snprintf(reinterpret_cast<char*>(ap_cfg.ap.ssid), sizeof ap_cfg.ap.ssid,
                                "PrusaCam-%02X%02X", mac[4], mac[5]);
    ap_cfg.ap.ssid_len = static_cast<uint8_t>(n);
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.password), ap_password.c_str(),
                 sizeof ap_cfg.ap.password);
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.channel = 6;
    esp_wifi_set_mode(WIFI_MODE_APSTA);  // STA side idle, but able to scan for networks
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    ap_active_ = true;
    ESP_LOGW(kTag, "No WiFi configured. Setup AP '%s', password '%s', http://192.168.4.1",
             reinterpret_cast<char*>(ap_cfg.ap.ssid), ap_password.c_str());
  }

  if ((err = esp_wifi_start()) != ESP_OK) return core::fail(core::Errc::Io, err);
  esp_wifi_set_ps(WIFI_PS_NONE);  // latency and stability over power: we are mains powered
  start_services(wifi.hostname);
  return {};
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
    if (self->connected_) ESP_LOGW(kTag, "disconnected (reason %d), reconnecting", ev->reason);
    self->connected_ = false;
    // The driver paces retries; the recovery ladder handles long outages.
    if (self->have_sta_) esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* ev = static_cast<ip_event_got_ip_t*>(data);
    char ip[16];
    std::snprintf(ip, sizeof ip, IPSTR, IP2STR(&ev->ip_info.ip));
    {
      std::lock_guard lock(self->mutex_);
      self->ip_ = ip;
    }
    self->connected_ = true;
    ESP_LOGI(kTag, "connected, IP %s", ip);
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
      case WIFI_AUTH_OPEN: auth = "abierta"; break;
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
