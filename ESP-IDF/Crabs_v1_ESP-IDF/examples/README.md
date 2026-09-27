# RAF_DTOS Module Templates

Copy one template into `main/input/`, rename it, and add one descriptor entry to `main/application_system/application.c`.

## Quick start

1. Copy the template matching your hardware.
2. Rename the `RAF_Example...` functions and fill in the TODO sections.
3. Add the module to `s_modules[]`:

```c
{
    .name = "MY_SENSOR",
    .init = RAF_MySensorInit,
    .task = {
        .name = "MY_SENSOR_Poll",
        .period_ms = 100,
        .priority = RAF_RT_PRIO_LOW,
        .core = RAF_RT_CORE_0,
        .stack_size = 3072,
        .callback = RAF_MySensorJob,
        .enabled_on_boot = true,
    },
    .required = false,
},
```

The application layer handles task registration. The module owns its driver state.

## Templates

- `module_i2c_simple.template.c`: periodic I2C sensor
- `module_spi_simple.template.c`: periodic SPI device
- `module_uart_parser.template.c`: UART receive and parser task
- `module_gpio_isr.template.c`: GPIO interrupt with task handoff
