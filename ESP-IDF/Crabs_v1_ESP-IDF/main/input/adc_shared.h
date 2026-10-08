/*
 * adc_shared.h
 *
 *  Created on: 8 Oct 2026
 *      Author: Rafdi
 */
 #ifndef ADC_SHARED_H
 #define ADC_SHARED_H

 #include "esp_err.h"
 #include "esp_adc/adc_oneshot.h"

 esp_err_t adc_shared_init(void);
 adc_oneshot_unit_handle_t adc_shared_get(void);

 #endif
