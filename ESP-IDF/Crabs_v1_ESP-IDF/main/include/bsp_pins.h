#ifndef MAIN_INCLUDE_BSP_PINS_H_
#define MAIN_INCLUDE_BSP_PINS_H_

typedef struct {
    int sda;
    int scl;
} bsp_i2c_pins_t;

typedef struct {
    int mosi;
    int miso;
    int sck;
    int cs;
} bsp_spi_pins_t;

typedef struct {
    int tx;
    int rx;
} bsp_uart_pins_t;

typedef struct {
    int tx;
    int rx;
} bsp_can_pins_t;

#endif /* MAIN_INCLUDE_BSP_PINS_H_ */