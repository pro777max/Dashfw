#include <stdlib.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"

void app_main(void) {
    bsp_display_start();
    bsp_display_brightness_set(100);
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "HELLO");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    bsp_display_unlock();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
