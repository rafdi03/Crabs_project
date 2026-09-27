/*

* task.h
*
* Created on: 4 Sept 2026
* ```
   Author: Rafdi
  ```

*/
#ifndef MAIN_INCLUDE_TASK_H_
#define MAIN_INCLUDE_TASK_H_

#pragma once

#include <stdint.h>
#include <stdbool.h>

void job_1ms(void *arg);
void job_5ms(void *arg);
void job_10ms(void *arg);
void job_15ms(void *arg);
void job_20ms(void *arg);
void job_50ms(void *arg);
void job_100ms(void *arg);
void job_200ms(void *arg);
void job_300ms(void *arg);
void job_500ms(void *arg);
void job_1000ms(void *arg);

#endif /* MAIN_INCLUDE_TASK_H_ */