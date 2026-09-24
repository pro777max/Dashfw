#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH_FINAL_FIX";

static void force_backlight_manual(void) {
    ESP_LOGI(TAG, "Attempting manual backlight takeover...");
    
    // ????????? 1: GPIO 26 (??????????? ??? ??? 1024x600 ? BSP, ??????? ??? ???? LEDC)
    gpio_config_t io_conf_26 = {
        .pin_bit_mask = (1ULL << 26),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err26 = gpio_config(&io_conf_26);
    if (err26 == ESP_OK) {
        gpio_set_level(GPIO_NUM_26, 1);
        ESP_LOGI(TAG, "SUCCESS: Forced GPIO 26 HIGH");
    } else {
        ESP_LOGW(TAG, "Failed to config GPIO 26: %s", esp_err_to_name(err26));
    }

    // ????????? 2: GPIO 23 (?? ??????, ???? ????? ??????? ??????? ??? ???????????? ????)
    gpio_config_t io_conf_23 = {
        .pin_bit_mask = (1ULL << 23),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err23 = gpio_config(&io_conf_23);
    if (err23 == ESP_OK) {
        gpio_set_level(GPIO_NUM_23, 1);
        ESP_LOGI(TAG, "SUCCESS: Forced GPIO 23 HIGH");
    } else {
        ESP_LOGW(TAG, "Failed to config GPIO 23: %s", esp_err_to_name(err23));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Final Fix Test...");

    // 1. ????????????? ??????? (?? ??? ???????, ??? ????????: "ek79007: version: 2.0.2")
    ESP_LOGI(TAG, "Init Display via BSP...");
    bsp_display_start();
    
    // 2. ???? ????? ?? ???????????? ??????? ??????
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. ?????? ?????? ??????????
    force_backlight_manual();

    // 4. ?????? ?????
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0); // ????-???????
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "FINAL\nFIX\nRED\nSCREEN");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_invalidate(scr);
    bsp_display_unlock();
    
    // 5. ?????????????? ???????????
    ESP_LOGI(TAG, "Forcing LVGL redraw...");
    lv_refr_now(NULL);
    
    ESP_LOGI(TAG, "If screen is RED, we finally won!");

    // 6. ???? ?????
    int counter = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        counter++;
        if (counter % 5 == 0) {
            ESP_LOGI(TAG, "System Alive... %d sec", counter);
        }
    }
}
