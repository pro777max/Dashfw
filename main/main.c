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

/* Handles */
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;

/* UI Objects */
static lv_obj_t *tach_arc = NULL;
static lv_obj_t *speed_arc = NULL;
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *lbl_odometer = NULL;
static lv_obj_t *lbl_temp = NULL;
static lv_obj_t *lbl_fuel = NULL;
static lv_obj_t *lbl_volt = NULL;

/* State variables */
static bool light_on = false;
static bool left_blink = false;
static bool right_blink = false;
static uint8_t blink_state = 0;
static uint32_t odometer_km = 12450;
static float engine_temp = 88.0f;
static float fuel_level = 65.0f;
static float battery_volt = 14.2f;

/* --- Backlight --- */
static void backlight_init(void) {
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_2, .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 1000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = { .gpio_num = DISP_BL_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_2, .timer_sel = LEDC_TIMER_2, .intr_type = LEDC_INTR_DISABLE, .duty = 819, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

/* --- Display Init --- */
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
    dpi_cfg.dpi_clock_freq_mhz = 40; // Fix underrun

    jd9165_vendor_config_t vendor_cfg = { .init_cmds = NULL, .init_cmds_size = 0, .mipi_config = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg } };
    esp_lcd_panel_dev_config_t panel_cfg = { .reset_gpio_num = DISP_RST_GPIO, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16, .vendor_config = &vendor_cfg };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(dbi_io, &panel_cfg, &panel_handle), TAG, "Panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "On");
    return ESP_OK;
}

/* --- Touch Init --- */
static esp_err_t touch_init(void) {
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = { .i2c_port = I2C_NUM_0, .sda_io_num = TOUCH_SDA, .scl_io_num = TOUCH_SCL, .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags = { .enable_internal_pullup = 1 } };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = 0x5D;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_cfg, &tp_io), TAG, "IO");

    static const esp_lcd_touch_io_gt911_config_t gt911_cfg = { .dev_addr = 0x5D };
    esp_lcd_touch_config_t tp_cfg = { .x_max = DISP_H_RES, .y_max = DISP_V_RES, .rst_gpio_num = TOUCH_RST, .int_gpio_num = TOUCH_INT, .levels = { .reset = 0, .interrupt = 0 }, .driver_data = (void *)&gt911_cfg };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp_handle), TAG, "GT911");
    return ESP_OK;
}

/* --- Callbacks --- */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!tp_handle) { data->state = LV_INDEV_STATE_RELEASED; return; }
    esp_lcd_touch_read_data(tp_handle);
    esp_lcd_touch_point_data_t pts[1]; uint8_t cnt = 0;
    if (esp_lcd_touch_get_data(tp_handle, pts, &cnt, 1) == ESP_OK && cnt > 0) {
        data->point.x = pts[0].x; data->point.y = pts[0].y; data->state = LV_INDEV_STATE_PRESSED;
    } else { data->state = LV_INDEV_STATE_RELEASED; }
}

static void tick_cb(void *arg) { lv_tick_inc(2); }

/* --- Event Handlers --- */
static void tach_click_cb(lv_event_t *e) {
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, tach_arc); lv_anim_set_values(&a, 0, 75);
    lv_anim_set_duration(&a, 1500); lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out); lv_anim_start(&a);
}
static void speed_click_cb(lv_event_t *e) {
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, speed_arc); lv_anim_set_values(&a, 0, 55);
    lv_anim_set_duration(&a, 2000); lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out); lv_anim_start(&a);
}
static void light_click_cb(lv_event_t *e) { light_on = !light_on; }
static void left_turn_click_cb(lv_event_t *e) { left_blink = !left_blink; }
static void right_turn_click_cb(lv_event_t *e) { right_blink = !right_blink; }

/* --- Timers --- */
static void blink_timer_cb(lv_timer_t *timer) {
    blink_state = !blink_state;
    lv_color_t on = lv_color_hex(0x00ff00); lv_color_t off = lv_color_hex(0x333333);
    if (left_blink) lv_obj_set_style_bg_color(left_turn, blink_state ? on : off, 0);
    if (right_blink) lv_obj_set_style_bg_color(right_turn, blink_state ? on : off, 0);
}

static void update_timer_cb(lv_timer_t *timer) {
    /* Time & Date */
    time_t now; struct tm ti; time(&now); localtime_r(&now, &ti);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text(lbl_time, buf);
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    lv_label_set_text(lbl_date, buf);

    /* Odometer simulation */
    int16_t sp = lv_arc_get_value(speed_arc);
    if (sp > 0) odometer_km += (sp * 220 / 100) / 3600; 
    snprintf(buf, sizeof(buf), "%lu km", (unsigned long)odometer_km);
    lv_label_set_text(lbl_odometer, buf);

    /* Gauges text */
    snprintf(buf, sizeof(buf), "%d", (int)((float)lv_arc_get_value(tach_arc) * 8000 / 100));
    lv_label_set_text(lbl_temp, buf); // Using temp label for RPM demo or add separate
    
    /* Simulate slight fluctuations */
    engine_temp = 88.0f + ((rand() % 100) / 100.0f - 0.5f);
    fuel_level -= 0.001f; if(fuel_level < 0) fuel_level = 0;
    battery_volt = 14.2f + ((rand() % 100) / 100.0f - 0.5f) * 0.2f;

    snprintf(buf, sizeof(buf), "%.1f?C", engine_temp);
    lv_label_set_text(lbl_temp, buf);
    snprintf(buf, sizeof(buf), "%.0f%%", fuel_level);
    lv_label_set_text(lbl_fuel, buf);
    snprintf(buf, sizeof(buf), "%.1fV", battery_volt);
    lv_label_set_text(lbl_volt, buf);
}

static void lvgl_task(void *arg) { while(1) { uint32_t t = lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(t < 5 ? 5 : t)); } }

/* --- Build Dashboard --- */
static void create_dashboard(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Top Bar: Time, Date, Odometer */
    lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_36, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 20, 10);

    lbl_date = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lbl_odometer = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_odometer, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_odometer, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_odometer, LV_ALIGN_TOP_RIGHT, -20, 20);

    /* Bottom Info Bar */
    lv_obj_t *info_bar = lv_obj_create(scr);
    lv_obj_set_size(info_bar, 600, 50);
    lv_obj_align(info_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(info_bar, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_radius(info_bar, 10, 0);
    lv_obj_set_flex_flow(info_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(info_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_temp = lv_label_create(info_bar);
    lv_obj_set_style_text_color(lbl_temp, lv_color_hex(0xff4444), 0);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_14, 0);
    
    lbl_fuel = lv_label_create(info_bar);
    lv_obj_set_style_text_color(lbl_fuel, lv_color_hex(0x44ff44), 0);
    lv_obj_set_style_text_font(lbl_fuel, &lv_font_montserrat_14, 0);

    lbl_volt = lv_label_create(info_bar);
    lv_obj_set_style_text_color(lbl_volt, lv_color_hex(0x4488ff), 0);
    lv_obj_set_style_text_font(lbl_volt, &lv_font_montserrat_14, 0);

    /* Tachometer (Left) */
    tach_arc = lv_arc_create(scr);
    lv_obj_set_size(tach_arc, 260, 260);
    lv_obj_align(tach_arc, LV_ALIGN_LEFT_MID, 40, 0);
    lv_arc_set_range(tach_arc, 0, 100); lv_arc_set_value(tach_arc, 0);
    lv_arc_set_bg_angles(tach_arc, 135, 405); lv_arc_set_rotation(tach_arc, 135);
    lv_obj_set_style_arc_width(tach_arc, 18, 0);
    lv_obj_set_style_arc_color(tach_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(tach_arc, lv_color_hex(0x00aaff), LV_PART_INDICATOR);
    lv_obj_set_flag(tach_arc, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(tach_arc, tach_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t_lbl = lv_label_create(scr);
    lv_label_set_text(t_lbl, "RPM");
    lv_obj_set_style_text_color(t_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(t_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align_to(t_lbl, tach_arc, LV_ALIGN_CENTER, 0, 60);

    /* Speedometer (Right) */
    speed_arc = lv_arc_create(scr);
    lv_obj_set_size(speed_arc, 260, 260);
    lv_obj_align(speed_arc, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_arc_set_range(speed_arc, 0, 100); lv_arc_set_value(speed_arc, 0);
    lv_arc_set_bg_angles(speed_arc, 135, 405); lv_arc_set_rotation(speed_arc, 135);
    lv_obj_set_style_arc_width(speed_arc, 18, 0);
    lv_obj_set_style_arc_color(speed_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(speed_arc, lv_color_hex(0x00ff88), LV_PART_INDICATOR);
    lv_obj_set_flag(speed_arc, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(speed_arc, speed_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *s_lbl = lv_label_create(scr);
    lv_label_set_text(s_lbl, "km/h");
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_lbl, speed_arc, LV_ALIGN_CENTER, 0, 60);

    /* Indicators Row */
    int ind_y = 80; int ind_sz = 50; int sp = 70;
    int sx = (DISP_H_RES - (4 * sp)) / 2 + 10;

    lv_obj_t *ind_light = lv_obj_create(scr);
    lv_obj_set_size(ind_light, ind_sz, ind_sz); lv_obj_align(ind_light, LV_ALIGN_TOP_MID, sx - sp, ind_y);
    lv_obj_set_style_bg_color(ind_light, lv_color_hex(0x333333), 0); lv_obj_set_style_radius(ind_light, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ind_light, 2, 0); lv_obj_set_style_border_color(ind_light, lv_color_hex(0xffdd00), 0);
    lv_obj_set_flag(ind_light, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(ind_light, light_click_cb, LV_EVENT_CLICKED, NULL);

    left_turn = lv_obj_create(scr);
    lv_obj_set_size(left_turn, ind_sz, ind_sz); lv_obj_align(left_turn, LV_ALIGN_TOP_MID, sx, ind_y);
    lv_obj_set_style_bg_color(left_turn, lv_color_hex(0x333333), 0); lv_obj_set_style_radius(left_turn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(left_turn, 2, 0); lv_obj_set_style_border_color(left_turn, lv_color_hex(0x00ff00), 0);
    lv_obj_set_flag(left_turn, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(left_turn, left_turn_click_cb, LV_EVENT_CLICKED, NULL);

    right_turn = lv_obj_create(scr);
    lv_obj_set_size(right_turn, ind_sz, ind_sz); lv_obj_align(right_turn, LV_ALIGN_TOP_MID, sx + sp, ind_y);
    lv_obj_set_style_bg_color(right_turn, lv_color_hex(0x333333), 0); lv_obj_set_style_radius(right_turn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(right_turn, 2, 0); lv_obj_set_style_border_color(right_turn, lv_color_hex(0x00ff00), 0);
    lv_obj_set_flag(right_turn, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(right_turn, right_turn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ind_eng = lv_obj_create(scr);
    lv_obj_set_size(ind_eng, ind_sz, ind_sz); lv_obj_align(ind_eng, LV_ALIGN_TOP_MID, sx + 2*sp, ind_y);
    lv_obj_set_style_bg_color(ind_eng, lv_color_hex(0x333333), 0); lv_obj_set_style_radius(ind_eng, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ind_eng, 2, 0); lv_obj_set_style_border_color(ind_eng, lv_color_hex(0xff6600), 0);
    lv_obj_set_flag(ind_eng, LV_OBJ_FLAG_CLICKABLE, true);

    lv_timer_create(blink_timer_cb, 500, NULL);
    lv_timer_create(update_timer_cb, 1000, NULL);
}

/* --- Main --- */
void app_main(void) {
    ESP_LOGI(TAG, "=== DASHBOARD START ===");
    backlight_init();
    ESP_ERROR_CHECK(display_init());
    if (touch_init() != ESP_OK) ESP_LOGW(TAG, "Touch failed");

    lv_init();
    lv_display_t *disp = lv_display_create(DISP_H_RES, DISP_V_RES);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    size_t buf_sz = DISP_H_RES * 40 * sizeof(lv_color_t);
    void *b1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    void *b2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, b1, b2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    if (tp_handle) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
    }

    const esp_timer_create_args_t ta = { .callback = tick_cb, .name = "tick" };
    esp_timer_handle_t tt; ESP_ERROR_CHECK(esp_timer_create(&ta, &tt));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tt, 2000));

    create_dashboard();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 0);

    while(1) { vTaskDelay(pdMS_TO_TICKS(10000)); ESP_LOGI(TAG, "alive"); }
}
