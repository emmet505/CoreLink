#include "wifi.h"

#include <string.h>

#include "dns_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"
#include "lwip/ip4_addr.h"
#include "sdkconfig.h"
#include "time_handler.h"
#include "wifi_config_store.h"

static const char* TAG = "WiFi";

static esp_netif_t* netif_ap = NULL;
static esp_netif_t* netif_sta = NULL;
static volatile int connected_clients = 0;
static volatile bool s_sta_connected = false;
static dns_server_handle_t s_dns_handle = NULL;
static uint8_t s_sta_retry = 0;
static uint8_t s_sta_max_retry = 5;

/* ── Helpers ─────────────────────────────────────────────────── */

static void dns_start(void) {
  if (s_dns_handle != NULL) return;
  dns_server_config_t cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
  s_dns_handle = start_dns_server(&cfg);
  ESP_LOGI(TAG, "DNS server started (captive portal active)");
}

static void dns_stop(void) {
  if (s_dns_handle == NULL) return;
  stop_dns_server(s_dns_handle);
  s_dns_handle = NULL;
  ESP_LOGI(TAG, "DNS server stopped (internet DNS active)");
}

/* ── Reconnect task ──────────────────────────────────────────── */

static void wifi_reconnect_task(void* arg) {
  uint32_t delay_ms = (uint32_t)arg;
  vTaskDelay(pdMS_TO_TICKS(delay_ms));
  ESP_LOGI(TAG, "Retrying STA connection...");
  esp_wifi_connect();
  vTaskDelete(NULL);
}

/* ── Event handler ───────────────────────────────────────────── */

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t* event =
        (wifi_event_ap_staconnected_t*)event_data;
    if (connected_clients < 8) connected_clients++;
    ESP_LOGI(TAG, "Client connected [MAC: %02x:%02x:%02x:%02x:%02x:%02x]",
             event->mac[0], event->mac[1], event->mac[2], event->mac[3],
             event->mac[4], event->mac[5]);

  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t* event =
        (wifi_event_ap_stadisconnected_t*)event_data;
    if (connected_clients > 0) connected_clients--;
    ESP_LOGI(TAG, "Client disconnected [MAC: %02x:%02x:%02x:%02x:%02x:%02x]",
             event->mac[0], event->mac[1], event->mac[2], event->mac[3],
             event->mac[4], event->mac[5]);

  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "Station started, connecting...");
    esp_wifi_connect();

  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_sta_connected = false;
    dns_start();

    if (s_sta_retry < s_sta_max_retry) {
      s_sta_retry++;

      uint32_t delay_ms = (1 << s_sta_retry) * 1000;
      if (delay_ms > 30000) delay_ms = 30000;
      ESP_LOGW(TAG, "STA disconnected, retry %d/%d in %lums", s_sta_retry,
               s_sta_max_retry, (unsigned long)delay_ms);
      xTaskCreate(wifi_reconnect_task, "wifi_retry", 2048,
                  (void*)(uintptr_t)delay_ms, 5, NULL);
    } else {
      ESP_LOGW(TAG, "STA max retries reached, staying as AP only");
      s_sta_retry = 0;
    }

  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t* event = (const ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "Station got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    s_sta_retry = 0;  // reset retry counter on success
    s_sta_connected = true;
    dns_stop();

    if (time_start_ntp() != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start SNTP after station connected");
    }

    // Forward DNS from router to AP clients
    esp_netif_dns_info_t dns;
    if (esp_netif_set_default_netif(netif_sta) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set default netif");
    }
    if (esp_netif_get_dns_info(netif_sta, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
      esp_netif_dhcps_stop(netif_ap);
      uint8_t offer_dns = 0x02;
      esp_netif_dhcps_option(netif_ap, ESP_NETIF_OP_SET,
                             ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns,
                             sizeof(offer_dns));
      esp_netif_set_dns_info(netif_ap, ESP_NETIF_DNS_MAIN, &dns);
      esp_netif_dhcps_start(netif_ap);
    }

#if CONFIG_LWIP_IPV4_NAPT
    if (esp_netif_napt_enable(netif_ap) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to enable NAT on AP interface");
    } else {
      ESP_LOGI(TAG, "NAT enabled on AP interface");
    }
#else
    ESP_LOGW(TAG, "NAT disabled — enable LWIP_IPV4_NAPT in menuconfig");
#endif
  }
}

/* ── wifi_init ───────────────────────────────────────────────── */

esp_err_t wifi_init(void) {
  ESP_LOGI(TAG, "Starting Wi-Fi (APSTA mode)");

  wifi_config_store_t cfg;
  esp_err_t err = wifi_config_load(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load Wi-Fi config: %s", esp_err_to_name(err));
    return err;
  }

  s_sta_max_retry = cfg.sta_max_retry;

  netif_ap = esp_netif_create_default_wifi_ap();
  netif_sta = esp_netif_create_default_wifi_sta();
  if (netif_ap == NULL || netif_sta == NULL) {
    ESP_LOGE(TAG, "Failed to create netif");
    return ESP_FAIL;
  }

  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&wifi_init_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi");
    return ESP_FAIL;
  }

  if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 &wifi_event_handler, NULL) != ESP_OK ||
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 &wifi_event_handler, NULL) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register event handlers");
    return ESP_FAIL;
  }

  /* ── AP config ── */
  wifi_config_t ap_config = {
      .ap =
          {
              .ssid_len = strlen(cfg.ssid),
              .channel = cfg.channel,
              .max_connection = cfg.max_connections,
              .authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };
  memcpy(ap_config.ap.ssid, cfg.ssid, sizeof(ap_config.ap.ssid));
  memcpy(ap_config.ap.password, cfg.password, sizeof(ap_config.ap.password));
  if (strlen(cfg.password) == 0) {
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  /* ── Country ── */
  wifi_country_t country = {
      .cc = "IR",
      .schan = 1,
      .nchan = 13,
      .policy = WIFI_COUNTRY_POLICY_MANUAL,
  };
  esp_wifi_set_country(&country);

  /* ── STA config — credentials from NVS ── */
  wifi_config_t sta_config = {
      .sta =
          {
              .scan_method = WIFI_FAST_SCAN,
              .failure_retry_cnt = 0,  // handled manually with backoff
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };
  strlcpy((char*)sta_config.sta.ssid, cfg.sta_ssid,
          sizeof(sta_config.sta.ssid));
  strlcpy((char*)sta_config.sta.password, cfg.sta_password,
          sizeof(sta_config.sta.password));

  /* ── Mode & config ── */
  if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &sta_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set Wi-Fi config");
    led_status_raise(LED_ERR_WIFI);
    return ESP_FAIL;
  }

  /* ── Static IP for AP ── */
  esp_netif_ip_info_t ip_info = {0};
  if (esp_netif_str_to_ip4(cfg.ip, &ip_info.ip) != ESP_OK) {
    ESP_LOGW(TAG, "Invalid IP, using default");
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
  }
  if (esp_netif_str_to_ip4(cfg.gateway, &ip_info.gw) != ESP_OK) {
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
  }
  if (esp_netif_str_to_ip4(cfg.netmask, &ip_info.netmask) != ESP_OK) {
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
  }

  if (esp_netif_dhcps_stop(netif_ap) != ESP_OK ||
      esp_netif_set_ip_info(netif_ap, &ip_info) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AP IP info");
    return ESP_FAIL;
  }

  /* ── DHCP option 114 (captive portal URI) ── */
  char portal_url[32];
  snprintf(portal_url, sizeof(portal_url), "http://%s/", cfg.ip);
  esp_err_t dhcp_err = esp_netif_dhcps_option(netif_ap, ESP_NETIF_OP_SET,
                                              (esp_netif_dhcp_option_id_t)114,
                                              portal_url, strlen(portal_url));
  if (dhcp_err != ESP_OK) {
    ESP_LOGW(TAG, "DHCP option 114 failed: %s", esp_err_to_name(dhcp_err));
  }

  if (esp_netif_dhcps_start(netif_ap) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start DHCP server");
    return ESP_FAIL;
  }

  if (esp_wifi_start() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Wi-Fi");
    led_status_raise(LED_ERR_WIFI);
    return ESP_FAIL;
  }

  /* Start DNS for captive portal — will be stopped when STA connects */
  dns_start();

  ESP_LOGI(TAG, "AP  SSID: %s | IP: %s | CH: %u | Max: %u", cfg.ssid, cfg.ip,
           cfg.channel, cfg.max_connections);
  ESP_LOGI(TAG, "STA SSID: %s", cfg.sta_ssid[0] ? cfg.sta_ssid : "(not set)");

  return ESP_OK;
}

/* ── wifi_stop ───────────────────────────────────────────────── */

void wifi_stop(void) {
  if (netif_ap == NULL) return;

  dns_stop();
  esp_wifi_stop();

#if CONFIG_LWIP_IPV4_NAPT
  esp_netif_napt_disable(netif_ap);
#endif

  esp_netif_destroy(netif_ap);
  esp_netif_destroy(netif_sta);
  netif_ap = NULL;
  netif_sta = NULL;
  ESP_LOGI(TAG, "Wi-Fi stopped");
}

/* ── Public API ──────────────────────────────────────────────── */

uint8_t wifi_get_connected_clients(void) { return connected_clients; }

bool wifi_is_sta_connected(void) { return s_sta_connected; }