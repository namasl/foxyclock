#include "xy_webpass.h"
#include <string.h>

const char *xy_web_pass_policy(const char *pass, bool changed)
{
    size_t n = strlen(pass);
    if (n == 0) return "web_pass: set a web page password (it protects the clock's settings and firmware)";
    if (n > XY_WEB_PASS_MAX) return "web_pass: must be at most 32 characters";
    if (changed && n < XY_WEB_PASS_MIN) return "web_pass: must be at least 8 characters";
    return NULL;
}

const char *xy_web_admin_gate(bool have_pass, bool setup_test)
{
    if (have_pass || setup_test) return NULL;
    return "set a web page password first (Security section), then try again";
}
