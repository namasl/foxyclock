#ifndef WEB_H
#define WEB_H
#include <stdbool.h>
#include "esp_http_server.h"

void web_start(void);
/* For handlers in other modules: checks the optional password; on failure
 * sends 401 and returns false. */
bool web_authorized(httpd_req_t *req);
/* web_authorized() plus the cross-site guard (X-XYClock: 1); use for every POST. */
bool web_post_allowed(httpd_req_t *req);
/* web_post_allowed() and a web password already set; use for admin actions. */
bool web_admin_allowed(httpd_req_t *req);
esp_err_t web_send_json_error(httpd_req_t *req, const char *status, const char *msg);
/* Restart the clock after a short delay, from a separate task. */
void web_restart_soon(void);

#endif
