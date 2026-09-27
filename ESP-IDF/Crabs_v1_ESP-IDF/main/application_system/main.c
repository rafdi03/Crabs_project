/*
 * main.c
 *
 *  Created on: 4 Sept 2026
 *      Author: Rafdi
 */
#include "main.h"
#include "application.h"
#include "esp_err.h"

 
 void app_main(void) {
   ESP_ERROR_CHECK(RAF_SchedulerInit());
   ESP_ERROR_CHECK(RAF_ApplicationInit());
   ESP_ERROR_CHECK(RAF_SchedulerStart());

 }