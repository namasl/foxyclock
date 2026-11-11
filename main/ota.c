/* POST /api/ota: firmware upload with integrity checks (design §11).
 *
 * The boot partition is switched only after every check passes:
 *  1. Content-Length present and fits the inactive slot;
 *  2. first byte is the image magic (0xE9);
 *  3. the stream contains an app descriptor whose project name is "xyclock"
 *     (core/imgscan.c; on ESP8266 it is not at a fixed offset);
 *  4. the byte count received equals Content-Length;
 *  5. esp_ota_end() verifies the image structure, its checksum and, with
 *     CONFIG_APP_UPDATE_CHECK_APP_HASH, the appended SHA-256.
 * Any failure leaves the running firmware selected. */
#include "ota.h"
#include <stdlib.h>
#include <string.h>
#include "clock_task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "web.h"
#include "xy_imgscan.h"

static const char *TAG = "ota";

#define CHUNK 1024

esp_err_t ota_handle_upload(httpd_req_t *req)
{
    if (!web_admin_allowed(req)) return ESP_OK;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return web_send_json_error(req, "500 Internal Server Error", "no update partition");
    size_t total = req->content_len;
    if (total == 0 || total > part->size)
        return web_send_json_error(req, "400 Bad Request", "file size does not fit the firmware slot");

    uint8_t *buf = malloc(CHUNK);
    xy_imgscan_t *scan = malloc(sizeof *scan);
    if (!buf || !scan) {
        free(buf); free(scan);
        return web_send_json_error(req, "500 Internal Server Error", "out of memory");
    }
    xy_imgscan_init(scan);

    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(part, total, &h);   /* erase only what is needed */
    if (err != ESP_OK) {
        free(buf); free(scan);
        return web_send_json_error(req, "500 Internal Server Error", "could not prepare the firmware slot");
    }
    clock_show_text("UPd", 0);
    ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)total, part->label);

    const char *fail = NULL;
    size_t got = 0;
    int found = 0, timeouts = 0;
    while (got < total && !fail) {
        size_t want = total - got < CHUNK ? total - got : CHUNK;
        int r = httpd_req_recv(req, (char *)buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (r <= 0) { fail = "upload interrupted before the whole file arrived"; break; }
        timeouts = 0;
        if (got == 0 && buf[0] != 0xE9) { fail = "not an ESP8266 firmware image"; break; }
        if (esp_ota_write(h, buf, (size_t)r) != ESP_OK) { fail = "flash write failed"; break; }
        got += (size_t)r;
        found = xy_imgscan_feed(scan, buf, (size_t)r, "xyclock");
        if (found < 0) { fail = "this firmware is for a different device (project name is not xyclock)"; break; }
    }
    char ver[32];
    snprintf(ver, sizeof ver, "%s", scan->version);
    free(buf);
    free(scan);

    if (!fail && got != total) fail = "upload incomplete";
    if (!fail && !found) fail = "no firmware description found: not an xyclock firmware";
    if (fail) {
        esp_ota_end(h);            /* releases the handle; result ignored */
        clock_clear_text();
        ESP_LOGW(TAG, "rejected: %s", fail);
        return web_send_json_error(req, "400 Bad Request", fail);
    }
    err = esp_ota_end(h);
    if (err != ESP_OK) {
        clock_clear_text();
        ESP_LOGW(TAG, "image verification failed: %s", esp_err_to_name(err));
        return web_send_json_error(req, "400 Bad Request",
                                   "image verification failed (checksum or SHA-256 mismatch: file corrupted)");
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        clock_clear_text();
        return web_send_json_error(req, "500 Internal Server Error", "could not select the new firmware");
    }
    ESP_LOGI(TAG, "firmware %s accepted; restarting", ver);
    httpd_resp_set_type(req, "application/json");
    char msg[96];
    snprintf(msg, sizeof msg, "{\"ok\":true,\"reboot\":true,\"version\":\"%s\"}", ver);
    httpd_resp_send(req, msg, strlen(msg));
    web_restart_soon();
    return ESP_OK;
}
