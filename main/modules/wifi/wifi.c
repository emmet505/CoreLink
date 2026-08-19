#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "wifi_config_store.h"
#include "dns_server.h"

static const char* TAG = "WiFi";
static esp_netif_t* netif_ap = NULL;
static volatile int connected_clients = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t* event =
        (wifi_event_ap_staconnected_t*)event_data;
    if (connected_clients < 8) {
      connected_clients++;
    }
    ESP_LOGI(TAG, "Client connected [MAC: %02x:%02x:%02x:%02x:%02x:%02x]",
             event->mac[0], event->mac[1], event->mac[2], event->mac[3],
             event->mac[4], event->mac[5]);
  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t* event =
        (wifi_event_ap_stadisconnected_t*)event_data;
    if (connected_clients > 0) {
      connected_clients--;
    }
    ESP_LOGI(TAG, "Client disconnected [MAC: %02x:%02x:%02x:%02x:%02x:%02x]",
             event->mac[0], event->mac[1], event->mac[2], event->mac[3],
             event->mac[4], event->mac[5]);
  }
}

esp_err_t wifi_init(void) {
  ESP_LOGI(TAG, "Starting Wi-Fi access point");

  wifi_config_store_t cfg;
  esp_err_t err = wifi_config_load(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load Wi-Fi config: %s", esp_err_to_name(err));
    return err;
  }

  netif_ap = esp_netif_create_default_wifi_ap();
  if (netif_ap == NULL) {
    ESP_LOGE(TAG, "Failed to create AP netif");
    return ESP_FAIL;
  }

  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&wifi_init_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi");
    return ESP_FAIL;
  }

  if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 &wifi_event_handler, NULL) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register event handler");
    return ESP_FAIL;
  }

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid_len = strlen(cfg.ssid),
              .channel = cfg.channel,
              .max_connection = cfg.max_connections,
              .authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };
  memcpy(wifi_config.ap.ssid, cfg.ssid, sizeof(wifi_config.ap.ssid));
  memcpy(wifi_config.ap.password, cfg.password,
         sizeof(wifi_config.ap.password));

  if (strlen(cfg.password) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set Wi-Fi mode");
    return ESP_FAIL;
  }

  if (esp_wifi_set_config(WIFI_IF_AP, &wifi_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set Wi-Fi config");
    return ESP_FAIL;
  }

  esp_netif_ip_info_t ip_info = {0};
  if (esp_netif_str_to_ip4(cfg.ip, &ip_info.ip) != ESP_OK) {
    ESP_LOGW(TAG, "Invalid IP string, using default");
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
  }
  if (esp_netif_str_to_ip4(cfg.gateway, &ip_info.gw) != ESP_OK) {
    ESP_LOGW(TAG, "Invalid gateway string, using default");
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
  }
  if (esp_netif_str_to_ip4(cfg.netmask, &ip_info.netmask) != ESP_OK) {
    ESP_LOGW(TAG, "Invalid netmask string, using default");
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
  }

  if (esp_netif_dhcps_stop(netif_ap) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to stop DHCP server");
    return ESP_FAIL;
  }

  if (esp_netif_set_ip_info(netif_ap, &ip_info) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set IP info");
    return ESP_FAIL;
  }
  
  const char *portal_url = "http://192.168.4.1/";
  esp_err_t dhcp_opt_err = esp_netif_dhcps_option(
      netif_ap,
      ESP_NETIF_OP_SET,
      (esp_netif_dhcp_option_id_t)114,
      (void *)portal_url,
      strlen(portal_url)
  );
  if (dhcp_opt_err != ESP_OK) {
      ESP_LOGW(TAG, "DHCP option 114 failed: %s", esp_err_to_name(dhcp_opt_err));
  } else {
      ESP_LOGI(TAG, "Captive portal URI set in DHCP");
  }


  if (esp_netif_dhcps_start(netif_ap) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start DHCP server");
    return ESP_FAIL;
  }

  if (esp_wifi_start() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Wi-Fi");
    return ESP_FAIL;
  }

dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
start_dns_server(&dns_config);


  ESP_LOGI(TAG, "Wi-Fi AP initialized");
  ESP_LOGI(TAG, "SSID: %s", cfg.ssid);
  ESP_LOGI(TAG, "Password: %s", cfg.password);
  ESP_LOGI(TAG, "IP: 192.168.4.1");
  ESP_LOGI(TAG, "Channel: %u", cfg.channel);
  ESP_LOGI(TAG, "Max connections: %u", cfg.max_connections);

  return ESP_OK;
}

void wifi_stop(void) {
  if (netif_ap != NULL) {
    esp_wifi_stop();
    esp_netif_destroy(netif_ap);
    netif_ap = NULL;
    ESP_LOGI(TAG, "Wi-Fi stopped");
  }
}

uint8_t wifi_get_connected_clients(void) { return connected_clients; }