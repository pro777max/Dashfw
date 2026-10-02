#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "DASH";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting display");
    bsp_display_start();

    ESP_LOGI(TAG, "Display started, drawing");
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();

    // ????? ???
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ??????? ??????? ????????????? ?? ??????
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 400, 200);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x00cc00), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    // ???????
    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, "DASH WORKS!");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

    bsp_display_unlock();

    // ???????? ????????? ????? BSP (?????????? ??????)
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "Done - backlight on");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
