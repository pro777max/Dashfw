#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "DASH_FINAL";

void app_main(void) {
    ESP_LOGI(TAG, "Starting EK79007 Final Test...");

    // 1. ????????????? ????? BSP (?? ???????? ?????????? ??????? ??? 1024x600)
    ESP_LOGI(TAG, "Init Display via BSP...");
    lv_display_t *disp = bsp_display_start();
    
    // 2. ????????????? ???????? ????????? ?? GPIO 23 (??????? ??? BSP ? GPIO 26)
    ESP_LOGI(TAG, "Forcing backlight GPIO 23 HIGH...");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 23),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_23, 1);
    ESP_LOGI(TAG, "Backlight forced ON");

    // 3. ???? ????? ?? ???????????? ????????????
    vTaskDelay(pdMS_TO_TICKS(300));

    // 4. ?????? ?????
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0); // ????-???????
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "EK79007\n1024x600\nRED");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    bsp_display_unlock();
    
    // 5. ?????????????? ???????????
    ESP_LOGI(TAG, "Forcing redraw...");
    lv_refr_now(disp);
    
    ESP_LOGI(TAG, "If screen is RED, configuration is finally correct!");

    // 6. ???? ?????
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "System Alive");
    }
}
