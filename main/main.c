#include <stdlib.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "DASH";

void app_main(void) {
    bsp_display_start();
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "HELLO");
    lv_obj_center(lbl);
    bsp_display_unlock();
    ESP_LOGI(TAG, "ui ready, starting backlight pin sweep");

    const int pins[] = {26, 25, 22, 21, 20, 19, 18, 17, 16, 15,
                        14, 13, 12, 11, 10, 9, 8, 7, 6, 5,
                        4, 3, 2, 1, 0};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_num_t p = (gpio_num_t)pins[i];
        gpio_reset_pin(p);
        gpio_set_direction(p, GPIO_MODE_OUTPUT);
        gpio_set_level(p, 1);
        ESP_LOGI(TAG, "BACKLIGHT TEST PIN %d HIGH", pins[i]);
        vTaskDelay(pdMS_TO_TICKS(2000));
        gpio_set_level(p, 0);
    }
    ESP_LOGI(TAG, "sweep done");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
