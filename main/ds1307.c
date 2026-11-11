#include "ds1307.h"
#include <string.h>
#include "board.h"
#include "i2c_bus.h"

static int bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static bool bcd_ok(uint8_t v) { return (v >> 4) <= 9 && (v & 0x0F) <= 9; }

static int days_in_month(int y, int m)
{
    static const uint8_t d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : d[m - 1];
}
static uint8_t tobcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

esp_err_t ds1307_read_utc(time_t *t, bool *valid)
{
    uint8_t r[7];
    i2c_bus_lock();
    esp_err_t err = i2c_bus_read_reg(DS1307_ADDR, 0x00, r, sizeof r);
    i2c_bus_unlock();
    *valid = false;
    if (err != ESP_OK) return err;
    if (r[0] & 0x80) return ESP_OK;                    /* CH: oscillator halted */
    /* Every field must be valid BCD (datasheet Table 2), else the RTC is not trusted. */
    if (!bcd_ok(r[0] & 0x7F) || !bcd_ok(r[1] & 0x7F) || !bcd_ok(r[2] & ((r[2] & 0x40) ? 0x1F : 0x3F)) ||
        !bcd_ok(r[4] & 0x3F) || !bcd_ok(r[5] & 0x1F) || !bcd_ok(r[6]))
        return ESP_OK;
    if ((r[2] & 0x40) && (bcd2(r[2] & 0x1F) < 1 || bcd2(r[2] & 0x1F) > 12))
        return ESP_OK;                                 /* 12-hour mode: hours are 1-12 */
    int sec = bcd2(r[0] & 0x7F), min = bcd2(r[1] & 0x7F);
    int hour = (r[2] & 0x40) ? (bcd2(r[2] & 0x1F) % 12 + ((r[2] & 0x20) ? 12 : 0))
                             : bcd2(r[2] & 0x3F);
    int day = bcd2(r[4] & 0x3F), mon = bcd2(r[5] & 0x1F), year = 2000 + bcd2(r[6]);
    if (year < 2024 || mon < 1 || mon > 12 || day < 1 || day > days_in_month(year, mon) ||
        hour > 23 || min > 59 || sec > 59)
        return ESP_OK;
    *t = (time_t)(days_from_civil(year, mon, day) * 86400 + hour * 3600 + min * 60 + sec);
    *valid = true;
    return ESP_OK;
}

esp_err_t ds1307_write_utc(time_t t)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    uint8_t r[7] = {
        tobcd(tm.tm_sec) & 0x7F,                       /* CH = 0: run */
        tobcd(tm.tm_min),
        tobcd(tm.tm_hour),                             /* 24 h mode */
        (uint8_t)(tm.tm_wday + 1),
        tobcd(tm.tm_mday),
        tobcd(tm.tm_mon + 1),
        tobcd(tm.tm_year % 100),
    };
    uint8_t buf[8];
    buf[0] = 0x00;                                     /* start at register 0 */
    memcpy(buf + 1, r, sizeof r);
    i2c_bus_lock();
    esp_err_t err = i2c_bus_write((uint8_t)(DS1307_ADDR << 1), buf, sizeof buf);
    i2c_bus_unlock();
    return err;
}
