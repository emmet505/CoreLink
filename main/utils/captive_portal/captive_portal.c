#include "captive_portal.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "captive_portal";

static esp_err_t captive_detect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive detection request: %s", req->uri);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    ESP_LOGI(TAG, "404 -> redirect: %s", req->uri);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t captive_portal_register(httpd_handle_t server)
{
    const char *detect_uris[] = {
        "/generate_204",
        "/gen_204",
        "/generate204",
        "/hotspot-detect.html",
        "/ncsi.txt",
        "/connecttest.txt",
        "/check_network_status.txt",
        "/success.txt",
        "/fwlink",
    };

    for (int i = 0; i < sizeof(detect_uris) / sizeof(detect_uris[0]); i++) {
        httpd_uri_t uri = {
            .uri      = detect_uris[i],
            .method   = HTTP_GET,
            .handler  = captive_detect_handler,
            .user_ctx = NULL,
        };
        esp_err_t ret = httpd_register_uri_handler(server, &uri);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register: %s", detect_uris[i]);
            return ret;
        }
    }


    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_handler);

    ESP_LOGI(TAG, "Captive portal registered");
    return ESP_OK;
}