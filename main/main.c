#include <stdlib.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "BLINK";

static void bl(int on) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, on ? 255 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

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
    while (1) {
        for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
            ledc_channel_config_t cc = {
                .gpio_num = pins[i],
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel = LEDC_CHANNEL_1,
                .timer_sel = LEDC_TIMER_1,
                .intr_type = LEDC_INTR_DISABLE,
                .duty = 0,
                .hpoint = 0,
            };
            ledc_channel_config(&cc);
            ESP_LOGI(TAG, "PIN %d: %d blinks", pins[i], (int)(i + 1));
            for (int b = 0; b < (int)(i + 1); b++) {
                bl(1);
                vTaskDelay(pdMS_TO_TICKS(600));
                bl(0);
                vTaskDelay(pdMS_TO_TICKS(600));
            }
            gpio_reset_pin((gpio_num_t)pins[i]);
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}
