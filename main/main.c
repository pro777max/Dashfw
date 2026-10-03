#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9165.h"
#include "esp_ldo_regulator.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

static const char *TAG = "DASH";

#define DISP_H_RES      1024
#define DISP_V_RES      600
#define DISP_RST_GPIO   5
#define DISP_BL_GPIO    23
#define TOUCH_SDA       7
#define TOUCH_SCL       8
#define TOUCH_INT       21
#define TOUCH_RST       22

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;

static lv_obj_t *tach_arc = NULL;
static lv_obj_t *tach_label = NULL;
static lv_obj_t *speed_arc = NULL;
static lv_obj_t *speed_label = NULL;
static lv_obj_t *light_ind = NULL;
static lv_obj_t *left_turn = NULL;
static lv_obj_t *right_turn = NULL;
static lv_obj_t *engine_ind = NULL;
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *lbl_odometer = NULL;
static lv_obj_t *lbl_temp = NULL;
static lv_obj_t *lbl_fuel = NULL;
static lv_obj_t *lbl_volt = NULL;

static bool light_on = false;
static bool engine_on = false;
static bool left_blink = false;
static bool right_blink = false;
static uint8_t blink_state = 0;
static uint32_t odometer_km = 12450;

static void backlight_init(void) {
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = DISP_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_2,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 819,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    ESP_LOGI(TAG, "Backlight ON");
}

static esp_err_t display_init(void) {
    ESP_LOGI(TAG, "Init JD9165...");
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "LDO");

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "DSI");

    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io), TAG, "DBI");

    esp_lcd_dpi_panel_config_t dpi_cfg = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi_cfg.dpi_clock_freq_mhz = 40;

    jd9165_vendor_config_t vendor_cfg = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .mipi_config = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISP_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi_io, &panel_cfg, &panel_handle), TAG, "Panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "On");
    ESP_LOGI(TAG, "Display ready %dx%d", DISP_H_RES, DISP_V_RES);
    return ESP_OK;
}

static esp_err_t touch_init(void) {
    ESP_LOGI(TAG, "Init GT911...");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = 1 },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = 0x5D;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io), TAG, "IO");

    static const esp_lcd_touch_io_gt911_config_t gt911_cfg = { .dev_addr = 0x5D };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISP_H_RES,
        .y_max = DISP_V_RES,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data = (void *)&gt911_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp_handle), TAG, "GT911");
    ESP_LOGI(TAG, "Touch ready");
    return ESP_OK;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!tp_handle) { data->state = LV_INDEV_STATE_RELEASED; return; }
    esp_lcd_touch_read_data(tp_handle);
    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    esp_err_t ret = esp_lcd_touch_get_data(tp_handle, pts, &cnt, 1);
    if (ret == ESP_OK && cnt > 0) {
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg) { lv_tick_inc(2); }

static void tach_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Tach clicked");
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tach_arc);
    lv_anim_set_values(&a, 0, 75);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void speed_click_cb(lv_event_t *e) {
    ESP_LOGI(TAG, "Speed clicked");
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, speed_arc);
    lv_anim_set_values(&a, 0, 55);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void light_click_cb(lv_event_t *e) {
    light_on = !light_on;
    lv_obj_set_style_bg_color(light_ind, light_on ? lv_color_hex(0xffdd00) : lv_color_hex(0x333333), 0);
    ESP_LOGI(TAG, "Light: %s", light_on ? "ON" : "OFF");
}

static void left_turn_click_cb(lv_event_t *e) {
    left_blink = !left_blink;
    if (!left_blink) lv_obj_set_style_bg_color(left_turn, lv_color_hex(0x333333), 0);
    ESP_LOGI(TAG, "Left: %s", left_blink ? "BLINK" : "OFF");
}

static void right_turn_click_cb(lv_event_t *e) {
    right_blink = !right_blink;
    if (!right_blink) lv_obj_set_style_bg_color(right_turn, lv_color_hex(0x333333), 0);
    ESP_LOGI(TAG, "Right: %s", right_blink ? "BLINK" : "OFF");
}

static void engine_click_cb(lv_event_t *e) {
    engine_on = !engine_on;
    lv_obj_set_style_bg_color(engine_ind, engine_on ? lv_color_hex(0xff6600) : lv_color_hex(0x333333), 0);
    ESP_LOGI(TAG, "Engine: %s", engine_on ? "ON" : "OFF");
}

static void blink_timer_cb(lv_timer_t *timer) {
    blink_state = !blink_state;
    lv_color_t on = lv_color_hex(0x00ff00);
    lv_color_t off = lv_color_hex(0x333333);
    if (left_blink) lv_obj_set_style_bg_color(left_turn, blink_state ? on : off, 0);
    if (right_blink) lv_obj_set_style_bg_color(right_turn, blink_state ? on : off, 0);
}

static void value_update_cb(lv_timer_t *timer) {
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text(lbl_time, buf);
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    lv_label_set_text(lbl_date, buf);

    int16_t sp = lv_arc_get_value(speed_arc);
    if (sp > 0) odometer_km += (uint32_t)((sp * 220) / 100) / 3600;
    snprintf(buf, sizeof(buf), "%lu km", (unsigned long)odometer_km);
    lv_label_set_text(lbl_odometer, buf);

    int16_t tach_val = lv_arc_get_value(tach_arc);
    int rpm = (tach_val * 8000) / 100;
    snprintf(buf, sizeof(buf), "%d", rpm);
    lv_label_set_text(tach_label, buf);

    int16_t speed_val = lv_arc_get_value(speed_arc);
    int speed = (speed_val * 220) / 100;
    snprintf(buf, sizeof(buf), "%d", speed);
    lv_label_set_text(speed_label, buf);

    float temp = 88.0f + ((rand() % 100) / 100.0f - 0.5f);
    float volt = 14.2f + ((rand() % 100) / 100.0f - 0.5f) * 0.2f;
    snprintf(buf, sizeof(buf), "%.1f C", temp);
    lv_label_set_text(lbl_temp, buf);
    snprintf(buf, sizeof(buf), "%.1f V", volt);
    lv_label_set_text(lbl_volt, buf);
    snprintf(buf, sizeof(buf), "%d%%", 65 - (int)(odometer_km % 30));
    lv_label_set_text(lbl_fuel, buf);
}

static void lvgl_task(void *arg) {
    while (1) {
        uint32_t t = lv_timer_handler();
        if (t < 5) t = 5;
        vTaskDelay(pdMS_TO_TICKS(t));
    }
}

static void create_dashboard(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    /* Top bar */
    lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 15, 8);

    lbl_date = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 3);

    lbl_odometer = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_odometer, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_odometer, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_odometer, LV_ALIGN_TOP_RIGHT, -15, 12);

    /* Bottom info bar */
    lv_obj_t *info_bar = lv_obj_create(scr);
    lv_obj_set_size(info_bar, 500, 40);
    lv_obj_align(info_bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(info_bar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_radius(info_bar, 8, 0);
    lv_obj_set_flex_flow(info_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(info_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_temp = lv_label_create(info_bar);
    lv_obj_set_style_text_color(lbl_temp, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_14, 0);

    lbl_fuel = lv_label_create(info_bar);
    lv_obj_set_style_text_color(lbl_fuel, lv_color_hex(0x44ff44), 0);
    lv_obj_set_style_text_font(lbl_fuel, &lv_font_montserrat_14, 0);

    lbl_volt = lv_label_create(info_bar);
    lv_obj_set_style_text_color(lbl_volt, lv_color_hex(0x4488ff), 0);
    lv_obj_set_style_text_font(lbl_volt, &lv_font_montserrat_14, 0);

    /* Tachometer - ???????? ?? 200px */
    tach_arc = lv_arc_create(scr);
    lv_obj_set_size(tach_arc, 200, 200);
    lv_obj_align(tach_arc, LV_ALIGN_LEFT_MID, 15, 15);
    lv_arc_set_range(tach_arc, 0, 100);
    lv_arc_set_value(tach_arc, 0);
    lv_arc_set_bg_angles(tach_arc, 135, 405);
    lv_arc_set_rotation(tach_arc, 135);
    lv_obj_set_style_arc_width(tach_arc, 12, 0);
    lv_obj_set_style_arc_color(tach_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(tach_arc, lv_color_hex(0x00aaff), LV_PART_INDICATOR);
    lv_obj_set_flag(tach_arc, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(tach_arc, tach_click_cb, LV_EVENT_CLICKED, NULL);

    tach_label = lv_label_create(scr);
    lv_label_set_text(tach_label, "0");
    lv_obj_set_style_text_color(tach_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(tach_label, &lv_font_montserrat_28, 0);
    lv_obj_align_to(tach_label, tach_arc, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t *tach_title = lv_label_create(scr);
    lv_label_set_text(tach_title, "RPM");
    lv_obj_set_style_text_color(tach_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(tach_title, &lv_font_montserrat_14, 0);
    lv_obj_align_to(tach_title, tach_arc, LV_ALIGN_CENTER, 0, 45);

    /* Speedometer - ???????? ?? 200px */
    speed_arc = lv_arc_create(scr);
    lv_obj_set_size(speed_arc, 200, 200);
    lv_obj_align(speed_arc, LV_ALIGN_RIGHT_MID, -15, 15);
    lv_arc_set_range(speed_arc, 0, 100);
    lv_arc_set_value(speed_arc, 0);
    lv_arc_set_bg_angles(speed_arc, 135, 405);
    lv_arc_set_rotation(speed_arc, 135);
    lv_obj_set_style_arc_width(speed_arc, 12, 0);
    lv_obj_set_style_arc_color(speed_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(speed_arc, lv_color_hex(0x00ff88), LV_PART_INDICATOR);
    lv_obj_set_flag(speed_arc, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(speed_arc, speed_click_cb, LV_EVENT_CLICKED, NULL);

    speed_label = lv_label_create(scr);
    lv_label_set_text(speed_label, "0");
    lv_obj_set_style_text_color(speed_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(speed_label, &lv_font_montserrat_28, 0);
    lv_obj_align_to(speed_label, speed_arc, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t *speed_title = lv_label_create(scr);
    lv_label_set_text(speed_title, "km/h");
    lv_obj_set_style_text_color(speed_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(speed_title, &lv_font_montserrat_14, 0);
    lv_obj_align_to(speed_title, speed_arc, LV_ALIGN_CENTER, 0, 45);

    /* Indicators - ????????? ?? 40px */
    int ind_y = 55;
    int ind_sz = 40;
    int sp = 60;
    int sx = DISP_H_RES / 2;

    light_ind = lv_obj_create(scr);
    lv_obj_set_size(light_ind, ind_sz, ind_sz);
    lv_obj_align(light_ind, LV_ALIGN_TOP_MID, sx - sp * 1.5, ind_y);
    lv_obj_set_style_bg_color(light_ind, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(light_ind, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(light_ind, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(light_ind, 2, 0);
    lv_obj_set_style_border_color(light_ind, lv_color_hex(0xffdd00), 0);
    lv_obj_set_flag(light_ind, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(light_ind, light_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *light_lbl = lv_label_create(scr);
    lv_label_set_text(light_lbl, "L");
    lv_obj_set_style_text_color(light_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(light_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align_to(light_lbl, light_ind, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    left_turn = lv_obj_create(scr);
    lv_obj_set_size(left_turn, ind_sz, ind_sz);
    lv_obj_align(left_turn, LV_ALIGN_TOP_MID, sx - sp * 0.5, ind_y);
    lv_obj_set_style_bg_color(left_turn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(left_turn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left_turn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(left_turn, 2, 0);
    lv_obj_set_style_border_color(left_turn, lv_color_hex(0x00ff00), 0);
    lv_obj_set_flag(left_turn, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(left_turn, left_turn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *left_lbl = lv_label_create(scr);
    lv_label_set_text(left_lbl, "<");
    lv_obj_set_style_text_color(left_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(left_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align_to(left_lbl, left_turn, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    right_turn = lv_obj_create(scr);
    lv_obj_set_size(right_turn, ind_sz, ind_sz);
    lv_obj_align(right_turn, LV_ALIGN_TOP_MID, sx + sp * 0.5, ind_y);
    lv_obj_set_style_bg_color(right_turn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(right_turn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_turn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(right_turn, 2, 0);
    lv_obj_set_style_border_color(right_turn, lv_color_hex(0x00ff00), 0);
    lv_obj_set_flag(right_turn, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(right_turn, right_turn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *right_lbl = lv_label_create(scr);
    lv_label_set_text(right_lbl, ">");
    lv_obj_set_style_text_color(right_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(right_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align_to(right_lbl, right_turn, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    engine_ind = lv_obj_create(scr);
    lv_obj_set_size(engine_ind, ind_sz, ind_sz);
    lv_obj_align(engine_ind, LV_ALIGN_TOP_MID, sx + sp * 1.5, ind_y);
    lv_obj_set_style_bg_color(engine_ind, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(engine_ind, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(engine_ind, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(engine_ind, 2, 0);
    lv_obj_set_style_border_color(engine_ind, lv_color_hex(0xff6600), 0);
    lv_obj_set_flag(engine_ind, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(engine_ind, engine_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *engine_lbl = lv_label_create(scr);
    lv_label_set_text(engine_lbl, "E");
    lv_obj_set_style_text_color(engine_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(engine_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align_to(engine_lbl, engine_ind, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    lv_timer_create(blink_timer_cb, 500, NULL);
    lv_timer_create(value_update_cb, 1000, NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== DASHBOARD START ===");
    backlight_init();
    ESP_ERROR_CHECK(display_init());
    esp_err_t tp_err = touch_init();
    if (tp_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch failed: %s", esp_err_to_name(tp_err));
    }

    lv_init();
    lv_display_t *disp = lv_display_create(DISP_H_RES, DISP_V_RES);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    size_t buf_size = DISP_H_RES * 40 * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    if (tp_handle) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_user_data(indev, tp_handle);
    }

    const esp_timer_create_args_t tick_args = { .callback = tick_cb, .name = "lvgl_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2000));

    create_dashboard();
    ESP_LOGI(TAG, "Dashboard ready");
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 0);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive");
    }
}
