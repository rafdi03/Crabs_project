/*
 * TDS.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

#ifndef MAIN_INCLUDE_TDS_H_
#define MAIN_INCLUDE_TDS_H_

#include "esp_err.h"
#include <stdbool.h>
#include "adc_shared.h"  

esp_err_t tds_init(void);
void      tds_update(void);
int       tds_get_ppm(void);
int       tds_get_raw_adc(void);
bool      tds_is_ready(void);

#endif
