#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH_TEST";

static void force_backlight_gpio23(void) {
    ESP_LOGI(TAG, "Forcing backlight on GPIO 23...");
    gpio_reset_pin(GPIO_NUM_23);
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER_1,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = 23,
        .duty           = 1023,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    ESP_LOGI(TAG, "Backlight is ON on GPIO 23");
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Display Test...");

    // 1. ????????????? ??????? ????? BSP (?????? ?? ?????, ??? ??? 1024x600 ? ???????? ??????? ???????????)
    ESP_LOGI(TAG, "Init Display (EK79007 1024x600)...");
    bsp_display_start();
    
    // 2. ?????????? ?????: ???? ??????????? ??????? 200 ?? ?? ????? ?? ?????? ???
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 3. ???????? ?????????
    force_backlight_gpio23();

    // 4. ?????? ???????? ?????
    bsp_display_lock(0);
    
    lv_obj_t *scr = lv_screen_active();
    
    // ????-??????? ???
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    // ????? ???????
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "TEST\nRED\nSCREEN\n1024x600");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_invalidate(scr);
    bsp_display_unlock();
    ESP_LOGI(TAG, "UI Created. Screen should be RED now.");

    // 5. ???? ?????
    int counter = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        counter++;
        if (counter % 5 == 0) {
            ESP_LOGI(TAG, "System Alive... %d sec", counter);
        }
    }
}
