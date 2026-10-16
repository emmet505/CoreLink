#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Send {"success":false,"error":"<msg>"} with the given HTTP status.
 * @param status e.g. "400 Bad Request"
 * @return always ESP_OK (so handlers can `return` it directly and keep the
 *         connection alive)
 */
esp_err_t http_send_json_error(httpd_req_t* req, const char* status,
                               const char* msg);

/**
 * @brief Send {"success":true}.
 * @return always ESP_OK (so handlers can return it directly)
 */
esp_err_t http_send_json_ok(httpd_req_t* req);

/**
 * @brief Read the whole request body (handles partial recv + timeouts).
 *
 * The body is heap-allocated and NUL-terminated; the caller must free() it.
 * On failure an error response has ALREADY been sent and NULL is returned,
 * so the handler should just `return ESP_OK;`.
 *
 * @param max_len maximum accepted Content-Length (1..max_len)
 */
char* http_read_body(httpd_req_t* req, size_t max_len);

/**
 * @brief http_read_body() + cJSON_Parse().
 *
 * Returns NULL after sending an error response (invalid size, receive
 * failure, out of memory, or invalid JSON). The caller owns the returned
 * object and must cJSON_Delete() it.
 */
cJSON* http_read_json(httpd_req_t* req, size_t max_len);