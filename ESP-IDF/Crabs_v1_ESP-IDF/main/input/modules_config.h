#ifndef MODULES_CONFIG_H
#define MODULES_CONFIG_H

#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

/* ============================================================
 * DHT22
 * ============================================================ */
#define DHT22_GPIO              2

/* ============================================================
 * DS18B20 (Water Temperature)
 * ============================================================ */
#define DS18B20_GPIO            15

/* ============================================================
 * TDS SENSOR
 * ============================================================ */
#define TDS_ADC_GPIO            34
#define TDS_ADC_CHANNEL         ADC_CHANNEL_6
#define TDS_ADC_ATTEN           ADC_ATTEN_DB_12
#define TDS_SAMPLE_COUNT        20

#define TDS_ADC_AIR_MURNI       3800
#define TDS_PPM_AIR_MURNI       10
#define TDS_ADC_AIR_KERAN       1500
#define TDS_PPM_AIR_KERAN       150

#define TDS_VREF                3.3f
#define TDS_CALIBRATION_A       1.0f
#define TDS_CALIBRATION_B       0.0f

/* ============================================================
 * JSN-SR04T (Ultrasonic Water Level)
 * ============================================================ */
#define JSN_TRIG_GPIO           32
#define JSN_ECHO_GPIO           36
#define JSN_MEDIAN_COUNT        5

/* ============================================================
 * DO SENSOR (Dissolved Oxygen)
 * ============================================================ */
 #define DO_ADC_GPIO             35
 #define DO_ADC_CHANNEL          ADC_CHANNEL_7
 #define DO_ADC_ATTEN            ADC_ATTEN_DB_12
 #define DO_SAMPLE_COUNT         10

/* ============================================================
 * LCD ILI9341 / ST7789
 * ============================================================ */
#define LCD_HOST_SPI            SPI2_HOST
#define LCD_PIN_SCLK            18
#define LCD_PIN_MOSI            19
#define LCD_PIN_CS              13
#define LCD_PIN_DC              12
#define LCD_PIN_RST             14
#define LCD_PIN_BL              -1

#define LCD_H_RES               320
#define LCD_V_RES               240
#define LCD_SPI_FREQ_HZ         (20 * 1000 * 1000)
#define LCD_SPI_MODE            0
#define LCD_TRANS_QUEUE_DEPTH   10

#define LCD_OFFSET_X            0
#define LCD_OFFSET_Y            0
#define LCD_ROTATION            0
#define LCD_INVERT_COLOR        true

/* ============================================================
 * RELAY (5 Channel, Active LOW)
 * ============================================================ */
#define RELAY_COUNT             5
#define RELAY_GPIO_1            25
#define RELAY_GPIO_2            22
#define RELAY_GPIO_3            0
#define RELAY_GPIO_4            33
#define RELAY_GPIO_5            21

#endif /* MODULES_CONFIG_H */