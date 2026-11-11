#ifndef DS1307_H
#define DS1307_H
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

/* RTC holds UTC. valid = oscillator running (CH clear) and a sane date. */
esp_err_t ds1307_read_utc(time_t *t, bool *valid);
/* Sets the time and starts the oscillator. */
esp_err_t ds1307_write_utc(time_t t);

#endif
