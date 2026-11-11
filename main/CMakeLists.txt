idf_component_register(SRCS "main.c" "beeper.c" "early_nvs.c" "clock_task.c" "ds1307.c" "i2c_bus.c"
                            "io.c" "net.c" "ntp.c" "ota.c" "store.c" "tm1650.c" "web.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "web/index.html"
                       REQUIRES core nvs_flash esp8266 freertos log pthread esp_event tcpip_adapter mdns esp_http_server json mbedtls app_update)

# newlib "nano" leaves float printf/scanf out unless asked. cJSON formats every
# number with %g, so without these all JSON replies with numbers fail.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _printf_float" "-u _scanf_float")

# Boot-time storage recovery: see early_nvs.c.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_mac_init")
