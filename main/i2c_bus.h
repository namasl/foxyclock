#ifndef I2C_BUS_H
#define I2C_BUS_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t i2c_bus_init(void);
void i2c_bus_lock(void);
void i2c_bus_unlock(void);
/* START, first byte, data..., STOP. Caller holds the lock. */
esp_err_t i2c_bus_write(uint8_t first, const uint8_t *data, size_t len);
/* Write register address to 7-bit device addr, then read len bytes. Caller holds the lock. */
esp_err_t i2c_bus_read_reg(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len);

#endif
