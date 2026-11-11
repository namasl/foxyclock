#ifndef OTA_H
#define OTA_H
#include "esp_http_server.h"

/* POST /api/ota: raw .bin body (design §11). */
esp_err_t ota_handle_upload(httpd_req_t *req);

#endif
