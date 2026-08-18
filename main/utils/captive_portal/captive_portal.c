#include "captive_portal.h"
#include "esp_log.h"

static const char *TAG = "captive_portal";

// Helper: همه رو redirect می‌کنه
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    ESP_LOGD(TAG, "redirected: %s", req->uri);
    return ESP_OK;
}

// Handler مخصوص Android — باید 204 برگردونه وقتی portal نیست
// ولی ما redirect می‌کنیم تا portal نشون داده بشه
static esp_err_t android_handler(httpd_req_t *req)
{
    return redirect_handler(req);
}

esp_err_t captive_portal_register(httpd_handle_t server)
{
    const httpd_uri_t uris[] = {
        { .uri = "/generate_204",        .method = HTTP_GET, .handler = android_handler  },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect_handler },
        { .uri = "/ncsi.txt",            .method = HTTP_GET, .handler = redirect_handler },
        { .uri = "/success.txt",         .method = HTTP_GET, .handler = redirect_handler },
        { .uri = "/redirect",            .method = HTTP_GET, .handler = redirect_handler },
        { .uri = "/canonical.html",      .method = HTTP_GET, .handler = redirect_handler },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t ret = httpd_register_uri_handler(server, &uris[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to register: %s", uris[i].uri);
            return ret;
        }
    }

    ESP_LOGI(TAG, "captive portal endpoints registered");
    return ESP_OK;
}