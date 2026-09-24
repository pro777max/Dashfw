#include <stdlib.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "DASH";

void app_main(void) {
    ESP_LOGI(TAG, "starting display...");
    bsp_display_start();
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "display started, backlight on");

    // ???????? ???????? ??????? LVGL
    lv_display_t *disp = lv_display_get_default();
    ESP_LOGI(TAG, "lv_display: %p, hres=%d, vres=%d",
             (void*)disp,
             disp ? lv_display_get_horizontal_resolution(disp) : -1,
             disp ? lv_display_get_vertical_resolution(disp) : -1);

    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    ESP_LOGI(TAG, "screen ptr: %p", (void*)scr);

    // ??????? ??? ?? ???? ?????
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_invalidate(scr);
    bsp_display_unlock();
    ESP_LOGI(TAG, "bg set to red, invalidated");

    // ?????????????? refresh ? ?????
    for (int i = 0; i < 20; i++) {
        bsp_display_lock(0);
        lv_timer_handler();
        lv_refr_now(NULL);
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "refresh loop done");

    // ??????? ????? ????? ? ?????? ??? ??????????? ??????
    bsp_display_lock(0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "DASH OK");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_obj_center(lbl);
    lv_obj_invalidate(scr);
    bsp_display_unlock();

    // ??? refresh
    for (int i = 0; i < 10; i++) {
        bsp_display_lock(0);
        lv_timer_handler();
        lv_refr_now(NULL);
        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "label drawn, entering idle loop");

    // ??????????? loop, ????? LVGL task ????????? ??????
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
