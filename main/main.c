#include <stdio.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"

static const char *TAG = "I2C_SCAN";

void app_main(void) {
    bsp_display_start();
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    bsp_display_unlock();
    
    bsp_i2c_init();
    i2c_master_bus_handle_t bus;
    bsp_i2c_get_handle(&bus);
    
    ESP_LOGI(TAG, "Scanning I2C bus 0x01-0x7F...");
    for (uint8_t addr = 1; addr < 128; addr++) {
        esp_err_t ret = i2c_master_probe(bus, addr, 100);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
        }
    }
    ESP_LOGI(TAG, "Scan complete");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
