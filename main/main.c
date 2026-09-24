#include <stdlib.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "PWM2";

void app_main(void) {
    bsp_display_start();
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    bsp_display_unlock();
    ESP_LOGI(TAG, "green fb ready");

    ledc_timer_config_t tc = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tc));

    const int pins[] = {26, 23, 24, 25, 28, 29, 30, 31, 32, 33,
                        45, 46, 47, 48, 49, 50, 51, 52, 53, 54};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        ledc_channel_config_t cc = {
            .gpio_num = pins[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .timer_sel = LEDC_TIMER_1,
            .intr_type = LEDC_INTR_DISABLE,
            .duty = 200,
            .hpoint = 0,
        };
        esp_err_t r = ledc_channel_config(&cc);
        ESP_LOGI(TAG, "PIN %d -> %s", pins[i], esp_err_to_name(r));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    const int freqs[] = {5000, 20000, 500};
    for (size_t f = 0; f < 3; f++) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, freqs[f]);
        ledc_channel_config_t cc = {
            .gpio_num = 26,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .timer_sel = LEDC_TIMER_1,
            .intr_type = LEDC_INTR_DISABLE,
            .duty = 255,
            .hpoint = 0,
        };
        ledc_channel_config(&cc);
        ESP_LOGI(TAG, "PIN 26 freq %d Hz duty 255", freqs[f]);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "sweep2 done");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
