#ifndef BOARD_H
#define BOARD_H

#include "esp_err.h"

#define DISPLAY_H_RES       240
#define DISPLAY_V_RES       280

// SPI LCD
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_MOSI        13
#define LCD_PIN_CLK         14
#define LCD_PIN_DC          21
#define LCD_PIN_RST         10
#define LCD_PIN_BCKL        8
#define LCD_SPI_CLK_HZ      (80 * 1000 * 1000)
#define LCD_BUF_SIZE        (DISPLAY_H_RES * DISPLAY_V_RES)  // Full frame
#define LCD_FLUSH_ROWS      60                                // Rows per flush buffer
#define LCD_FLUSH_BUF       (LCD_FLUSH_ROWS * DISPLAY_H_RES) // Pixels per flush buffer

// I2C Touch
#define TP_I2C_NUM          I2C_NUM_0
#define TP_PIN_SCL          12
#define TP_PIN_SDA          11
#define TP_PIN_RST          9
#define TP_PIN_INT          3
#define TP_I2C_CLK_HZ       (400000)

// LED GPIO
#define LED_PIN_0           38
#define LED_PIN_1           39
#define LED_PIN_2           40
#define LED_PIN_3           41

// Touch debug: 1 = show white dot at touch point on all pages
#define TOUCH_DEBUG         0
#define STATUS_BAR_HEIGHT   28
#define APP_AREA_Y          STATUS_BAR_HEIGHT
#define APP_AREA_H          (DISPLAY_V_RES - STATUS_BAR_HEIGHT)

// LEDC Backlight
#define BCKL_LEDC_SPEED     LEDC_LOW_SPEED_MODE
#define BCKL_LEDC_TIMER     LEDC_TIMER_0
#define BCKL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BCKL_LEDC_DUTY_RES  LEDC_TIMER_10_BIT
#define BCKL_LEDC_FREQ_HZ   5000

#endif // BOARD_H
