#include "backlight.h"
#include "board.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "backlight";
static int s_percent = 0;

esp_err_t board_backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = BCKL_LEDC_SPEED,
        .timer_num = BCKL_LEDC_TIMER,
        .duty_resolution = BCKL_LEDC_DUTY_RES,
        .freq_hz = BCKL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = LCD_PIN_BCKL,
        .speed_mode = BCKL_LEDC_SPEED,
        .channel = BCKL_LEDC_CHANNEL,
        .timer_sel = BCKL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
    ESP_LOGI(TAG, "Backlight LEDC initialized on GPIO %d", LCD_PIN_BCKL);
    return ESP_OK;
}

void board_backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_percent = percent;
    uint32_t duty = (1023 * percent) / 100;
    ledc_set_duty(BCKL_LEDC_SPEED, BCKL_LEDC_CHANNEL, duty);
    ledc_update_duty(BCKL_LEDC_SPEED, BCKL_LEDC_CHANNEL);
}

int board_backlight_get(void)
{
    return s_percent;
}
