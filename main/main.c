#include <stdlib.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "DIAG4";

void app_main(void) {
    bsp_display_start();
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    bsp_display_unlock();

    ledc_timer_config_t tc = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tc));

    const int pins[] = {26, 23, 24, 25};
    int prev = -1;
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (prev >= 0) {
            gpio_reset_pin((gpio_num_t)prev);
        }
        ledc_channel_config_t cc = {
            .gpio_num = pins[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .timer_sel = LEDC_TIMER_1,
            .intr_type = LEDC_INTR_DISABLE,
            .duty = 128,
            .hpoint = 0,
        };
        esp_err_t r = ledc_channel_config(&cc);
        ESP_LOGI(TAG, "TEST PIN %d -> %s (watch screen!)", pins[i], esp_err_to_name(r));
        vTaskDelay(pdMS_TO_TICKS(2500));
        prev = pins[i];
    }
    if (prev >= 0) {
        gpio_reset_pin((gpio_num_t)prev);
    }
    ESP_LOGI(TAG, "diag4 done, backlight OFF now");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
