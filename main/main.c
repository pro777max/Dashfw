#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH";
static lv_obj_t *tt = NULL;

static void i2c_bus_recovery(void) {
    const int pins[] = {8, 22};
    for (int p = 0; p < 2; p++) {
        gpio_set_pull_mode((gpio_num_t)pins[p], GPIO_PULLUP_ONLY);
        gpio_set_direction((gpio_num_t)pins[p], GPIO_MODE_OUTPUT_OD);
        for (int i = 0; i < 9; i++) {
            gpio_set_level((gpio_num_t)pins[p], 0);
            vTaskDelay(pdMS_TO_TICKS(5));
            gpio_set_level((gpio_num_t)pins[p], 1);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        gpio_set_direction((gpio_num_t)pins[p], GPIO_MODE_INPUT);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "i2c bus recovery done");
}

void app_main(void) {
    ESP_LOGI(TAG, "boot: i2c recovery first");
    i2c_bus_recovery();

    ESP_LOGI(TAG, "backlight gpio23: 1kHz, 80%%");
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = 23,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_2,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 819,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));

    ESP_LOGI(TAG, "display start");
    bsp_display_start();
    ESP_LOGI(TAG, "lvgl %d.%d", (int)lv_version_major(), (int)lv_version_minor());

    lv_display_t *dd = lv_display_get_default();
    if (dd) {
        ESP_LOGI(TAG, "lvgl disp res: %dx%d",
                 (int)lv_display_get_horizontal_resolution(dd),
                 (int)lv_display_get_vertical_resolution(dd));
    } else {
        ESP_LOGE(TAG, "lvgl default display is NULL!");
    }

    bool locked = bsp_display_lock(2000);
    ESP_LOGI(TAG, "lock(2000) = %d", (int)locked);
    if (locked) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_set_size(box, 500, 140);
        lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x00cc00), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 6, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(0xff0000), 0);
        tt = lv_label_create(box);
        lv_label_set_text(tt, "DASH ALIVE");
        lv_obj_set_style_text_color(tt, lv_color_black(), 0);
        lv_obj_center(tt);

        lv_refr_now(NULL);
        bsp_display_unlock();
        ESP_LOGI(TAG, "canary drawn + refr_now");
    }

    int n = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        n++;
        if (n == 3 && tt) {
            if (bsp_display_lock(1000)) {
                lv_label_set_text(tt, "RENDER 3s OK");
                lv_refr_now(NULL);
                bsp_display_unlock();
                ESP_LOGI(TAG, "label updated at 3 sec");
            } else {
                ESP_LOGW(TAG, "lock failed at 3 sec");
            }
        }
        if (n % 10 == 0) ESP_LOGI(TAG, "alive %d sec", n);
    }
}
