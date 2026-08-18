#include "dns_server.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_netif.h"

#define DNS_PORT        53
#define DNS_BUF_SIZE    512
#define CAPTIVE_IP      "192.168.4.1"
#define DNS_TASK_STACK  4096
#define DNS_TASK_PRIO   5

static const char *TAG = "dns_server";
static TaskHandle_t s_dns_task = NULL;
static int s_sock = -1;

// DNS header — packed to avoid alignment padding
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS answer record (pointer form)
typedef struct __attribute__((packed)) {
    uint16_t name_ptr;    // 0xC00C — pointer to question name
    uint16_t type;        // A record = 1
    uint16_t class;       // IN = 1
    uint32_t ttl;         // seconds
    uint16_t rd_length;   // 4 for IPv4
    uint8_t  rdata[4];    // IP address
} dns_answer_t;

static void dns_task(void *arg)
{
    uint8_t buf[DNS_BUF_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int len = recvfrom(s_sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *hdr = (dns_header_t *)buf;

        // Only handle standard queries (QR=0, OPCODE=0)
        if (ntohs(hdr->flags) & 0x8000) {
            continue;
        }

        // Build response in-place — question section stays untouched
        hdr->flags    = htons(0x8180); // QR=1, AA=1, RCODE=0
        hdr->an_count = htons(1);
        hdr->ns_count = 0;
        hdr->ar_count = 0;

        // Append answer after the existing packet
        dns_answer_t *ans = (dns_answer_t *)(buf + len);
        ans->name_ptr  = htons(0xC00C); // pointer to byte 12 (start of QNAME)
        ans->type      = htons(1);      // A record
        ans->class     = htons(1);      // IN
        ans->ttl       = htonl(60);
        ans->rd_length = htons(4);

        // Parse captive IP into answer
        esp_ip4_addr_t ip;
        esp_netif_str_to_ip4(CAPTIVE_IP, &ip);
        memcpy(ans->rdata, &ip.addr, 4);

        int resp_len = len + sizeof(dns_answer_t);
        sendto(s_sock, buf, resp_len, 0,
               (struct sockaddr *)&client_addr, client_len);

        ESP_LOGD(TAG, "answered DNS query → %s", CAPTIVE_IP);
    }
}

esp_err_t dns_server_start(void)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreate(dns_task, "dns_server",
                                 DNS_TASK_STACK, NULL,
                                 DNS_TASK_PRIO, &s_dns_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed");
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_dns_task) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    ESP_LOGI(TAG, "DNS server stopped");
}