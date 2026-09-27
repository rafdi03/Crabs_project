/*
 * modules_config.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
 
 #ifndef MAIN_INCLUDE_MODULES_CONFIG_H_
 #define MAIN_INCLUDE_MODULES_CONFIG_H_

 // ==== DHT22 ====
 #define DHT22_GPIO              2
 #define DHT22_TYPE              22

 // ==== DS18B20 ====
 #define DS18B20_GPIO            15

 // ==== TDS (ADC) ====
 #define TDS_ADC_CHANNEL         6
 #define TDS_ADC_ATTEN           3
 #define TDS_SAMPLE_COUNT        20
 #define TDS_ADC_AIR_MURNI       3800
 #define TDS_PPM_AIR_MURNI       10
 #define TDS_ADC_AIR_KERAN       1500
 #define TDS_PPM_AIR_KERAN       150

 // ==== JSN-SR04T ====
 #define JSN_TRIG_GPIO           32
 #define JSN_ECHO_GPIO           36
 #define JSN_MEDIAN_COUNT        5

 // ==== DO Sensor (Dummy) ====
 #define DO_BASE_DEFAULT         6.5f
 #define DO_MIN_VALID            3.0f
 #define DO_MAX_VALID            9.0f

 // ==== LCD ILI9342 / ILI9341 compatible 320x240 ====
 #define LCD_HOST_SPI            SPI2_HOST

 #define LCD_PIN_SCLK            18
 #define LCD_PIN_MOSI            19
 #define LCD_PIN_CS              13     
 #define LCD_PIN_DC              12
 #define LCD_PIN_RST             14
 #define LCD_PIN_BL              -1

 #define LCD_H_RES               320    
 #define LCD_V_RES               240    

 #define LCD_SPI_FREQ_HZ         (5 * 1000 * 1000)   // ← dari 20 MHz ke 10 MHz

 #define LCD_SPI_MODE            0
 #define LCD_OFFSET_X            0
 #define LCD_OFFSET_Y            0
 #define LCD_ROTATION            0
 #define LCD_INVERT_COLOR        false 
 #define LCD_TRANS_QUEUE_DEPTH   1

 // ==== Relay ====
 #define RELAY_COUNT             5
 #define RELAY_GPIO_1            25
 #define RELAY_GPIO_2            26
 #define RELAY_GPIO_3            0
 #define RELAY_GPIO_4            33
 #define RELAY_GPIO_5            21

 #endif