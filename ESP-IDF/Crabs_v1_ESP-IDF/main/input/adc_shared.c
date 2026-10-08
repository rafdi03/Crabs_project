/*
 * adc_shared.c
 *
 *  Created on: 8 Oct 2026
 *      Author: Rafdi
 */
 
 #include "adc_shared.h"
 #include "esp_log.h"

 static const char *TAG = "ADC_SHARED";
 static adc_oneshot_unit_handle_t s_adc = NULL;

 esp_err_t adc_shared_init(void)
 {
     if (s_adc != NULL) return ESP_OK;

     adc_oneshot_unit_init_cfg_t cfg = {
         .unit_id  = ADC_UNIT_1,
         .ulp_mode = ADC_ULP_MODE_DISABLE,
     };
     esp_err_t ret = adc_oneshot_new_unit(&cfg, &s_adc);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "new_unit failed: %s", esp_err_to_name(ret));
         return ret;
     }
     ESP_LOGI(TAG, "ADC unit 1 siap dipakai bersama");
     return ESP_OK;
 }

 adc_oneshot_unit_handle_t adc_shared_get(void)
 {
     return s_adc;
 }

