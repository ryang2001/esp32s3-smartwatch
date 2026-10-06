#include "display.h"
#include "board.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "driver/spi_master.h"
#include "lvgl.h"

static const char *TAG = "display";
static lv_display_t *s_disp = NULL;

esp_err_t board_display_init(void)
{
    // SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_H_RES * 2 * 20,  /* 9600 B: fewer DMA interrupts per frame */
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");

    // Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = -1,
        .pclk_hz = LCD_SPI_CLK_HZ,
        .spi_mode = 3,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.sio_mode = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &io_handle));

    // ST7789 panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 20));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "ST7789 panel initialized");

    // LVGL port
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_priority = 4;
    lvgl_cfg.task_stack = 16384;
    lvgl_cfg.task_affinity = 1;
    lvgl_cfg.timer_period_ms = 10;
    lvgl_cfg.task_max_sleep_ms = 50;
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Display — partial refresh with 60-row PSRAM double buffer
    // Only redraws dirty regions instead of the full 240x280 frame each time
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_FLUSH_BUF,
        .double_buffer = true,
        .hres = DISPLAY_H_RES,
        .vres = DISPLAY_V_RES,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags.buff_spiram = 1,
        .flags.swap_bytes = 1,
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_LOGI(TAG, "LVGL display added, disp=%p", s_disp);
    return ESP_OK;
}

lv_display_t *board_get_display(void)
{
    return s_disp;
}
