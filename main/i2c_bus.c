/* Shared bus for the TM1650 (pseudo-I2C, register = address byte) and DS1307. */
#include "i2c_bus.h"
#include "board.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;

esp_err_t i2c_bus_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,   /* 4.7k external pull-ups */
        .scl_io_num = PIN_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .clk_stretch_tick = 300,
    };
    esp_err_t err = i2c_driver_install(I2C_PORT, conf.mode);
    if (err == ESP_OK) err = i2c_param_config(I2C_PORT, &conf);
    return err;
}

void i2c_bus_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
void i2c_bus_unlock(void) { xSemaphoreGive(s_lock); }

esp_err_t i2c_bus_write(uint8_t first, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, first, true);
    if (len) i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t i2c_bus_read_reg(uint8_t addr7, uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(addr7 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(addr7 << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}
