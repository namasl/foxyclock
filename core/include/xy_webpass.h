/* Web page password policy (owner decision 2026-11-10: a password is
 * required). Kept out of xy_settings_validate() on purpose: settings saved by
 * older firmware have no password, and must still load (the page then asks
 * for one). */
#ifndef XY_WEBPASS_H
#define XY_WEBPASS_H
#include <stdbool.h>

#define XY_WEB_PASS_MIN 8
#define XY_WEB_PASS_MAX 32

/* Checks the web password a settings save would leave in place. changed:
 * the save sets a new password (so the minimum length applies). Returns NULL
 * if acceptable, else a message for the page. */
const char *xy_web_pass_policy(const char *pass, bool changed);

/* For administrative actions other than saving settings (GPIO14, restart,
 * factory reset, firmware upload, connection test): NULL if allowed.
 * have_pass: a password is set. setup_test: this is a connection test in
 * setup mode (allowed before the password is chosen). */
const char *xy_web_admin_gate(bool have_pass, bool setup_test);

#endif
