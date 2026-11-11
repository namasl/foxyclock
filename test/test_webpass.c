#include "minitest.h"
#include "xy_webpass.h"

static void test_save_requires_password(void)
{
    ASSERT_CONTAINS(xy_web_pass_policy("", false), "password");
    ASSERT_CONTAINS(xy_web_pass_policy("", true), "password");
}

static void test_new_password_min_length(void)
{
    ASSERT_CONTAINS(xy_web_pass_policy("short", true), "8");
    ASSERT(xy_web_pass_policy("longenough", true) == NULL);
    ASSERT(xy_web_pass_policy("12345678", true) == NULL);
}

static void test_existing_short_password_kept(void)
{
    /* A password set before the minimum existed is not rejected on every save. */
    ASSERT(xy_web_pass_policy("abc", false) == NULL);
}

static void test_too_long(void)
{
    ASSERT_CONTAINS(xy_web_pass_policy("0123456789012345678901234567890123", true), "32");
}

static void test_admin_gate(void)
{
    /* Changes other than setting the password need one to exist already. */
    ASSERT_CONTAINS(xy_web_admin_gate(false, false), "password");
    ASSERT(xy_web_admin_gate(true, false) == NULL);
    /* The connection test is allowed without one, only in setup mode. */
    ASSERT(xy_web_admin_gate(false, true) == NULL);
}

int main(void)
{
    RUN(test_save_requires_password);
    RUN(test_new_password_min_length);
    RUN(test_existing_short_password_kept);
    RUN(test_too_long);
    RUN(test_admin_gate);
    return TEST_SUMMARY("test_webpass");
}
