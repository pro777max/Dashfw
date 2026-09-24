#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "lvgl.h"

// Подключаем внутренние заголовки BSP напрямую
#include "bsp/esp-bsp.h"
// Пытаемся найти определения EK79007 через внутренние пути компонента
// В зависимости от версии BSP пути могут отличаться, попробуем стандартный для esp-bsp
#include "esp_lcd_panel_vendor.h" 

// Если стандартный путь не сработает, мы определим структуры вручную, 
// опираясь на логику из исходников BSP, которые мы видели ранее.

static const char *TAG = "DASH_DIRECT";

static lv_display_t *lvgl_disp = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    if (panel_handle) {
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }
    lv_display_flush_ready(disp);
}

// Ручное определение структур, если они не экспортированы в публичные заголовки
// (Это копия того, что лежит внутри esp32_p4_function_ev_board.c)
typedef struct {
    struct {
        esp_lcd_dsi_bus_handle_t dsi_bus;
        esp_lcd_dpi_panel_config_t *dpi_config;
    } mipi_config;
} ek79007_vendor_config_t;

static void init_ek79007_direct(void) {
    ESP_LOGI(TAG, "Initializing MIPI DSI bus (2 lanes)...");
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = LCD_PHY_CLK_SRC_PLL_FPLL,
        .lane_bit_rate_mbps = 1000, // 1 Gbps
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus));

    ESP_LOGI(TAG, "Installing DSI Panel IO...");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .dc_gpio_num = -1,
        .cs_gpio_num = -1,
        .pclk_hz = 10000000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &io_handle));

    ESP_LOGI(TAG, "Configuring EK79007 DPI parameters manually...");
    // Прямая настройка DPI конфигурации для 1024x600 @ 60Hz
    esp_lcd_dpi_panel_config_t dpi_config = {
        .clock_source = LCD_CLK_SRC_PLL160M, // Или другой доступный источник
        .timings = {
            .pclk_hz = 51200000, // Примерная частота пиксельного клока для 60Гц
            .h_res = 1024,
            .v_res = 600,
            .hsync_pulse_width = 20,
            .hsync_back_porch = 10,
            .hsync_front_porch = 10,
            .vsync_pulse_width = 10,
            .vsync_back_porch = 10,
            .vsync_front_porch = 10,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16, // RGB565
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = 0,
    };

    ek79007_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = 27, // GPIO 27 для сброса
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    
    // Вызываем функцию инициализации EK79007. 
    // Если она не экспортирована в заголовки, нам придется использовать bsp_display_start() 
    // но с "подменой" конфигурации, либо искать альтернативный путь.
    
    // ПОПЫТКА 1: Использовать pxt_func указатель или прямой вызов, если символ доступен
    // Так как мы не можем гарантировать наличие esp_lcd_new_panel_ek79007 в линковке без заголовка,
    // мы вернемся к bsp_display_start(), но перед этим жестко зададим ENV переменную или дефайн?
    // Нет, лучший способ - использовать "Weak" символы или патч Kconfig.
    
    // АЛЬТЕРНАТИВА: Поскольку мы знаем, что bsp_display_start() работает с EK79007 при правильной конфиге,
    // а проблема только в выборе ILI9881C, давайте попробуем "обмануть" BSP, подменив функцию выбора панели?
    // Слишком сложно.
    
    // ДАВАЙТЕ ПРОСТО ИСПОЛЬЗУЕМ bsp_display_start(), НО ПЕРЕД ЭТИМ МЫ УЖЕ ЗНАЕМ, ЧТО ОН ПАДАЕТ.
    
    // НОВЫЙ ПЛАН: Мы используем компонент esp_lcd_jd9165, который мы успешно скачали ранее!
    // В логах CI было: NOTICE: [2/5] espressif/esp_lcd_jd9165 (1.0.4)
    // Но заводская прошивка говорила про EK79007. 
    
    // СТОП. В логе ошибки компиляции JD9165 мы видели, что компонент скачался.
    // А в логе работы платы мы видели: E (4190) ili9881c: ... failed.
    // Это значит, что BSP выбрал НЕПРАВИЛЬНЫЙ драйвер.
    
    // ЕДИНСТВЕННЫЙ НАДЕЖНЫЙ СПОСОБ БЕЗ ЗАГОЛОВКОВ:
    // Использовать bsp_display_new_with_handles, передав ему наши настройки? Нет, это private API.
    
    // ДАВАЙТЕ ВЕРНЕМСЯ К ИДЕЕ С SDKCONFIG.DEFAULTS, НО СДЕЛАЕМ ЭТО ЧЕРЕЗ CMAKE_FLAGS.
    // Или просто добавим дефайн в CMakeLists.txt проекта!
    
    ESP_LOGE(TAG, "Direct init blocked by missing headers. Falling back to plan B.");
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting...");
    // Пока просто заглушка, чтобы проверить компиляцию
    while(1) vTaskDelay(1000);
}
