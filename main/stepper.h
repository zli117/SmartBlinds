#ifndef STEPPER_H_
#define STEPPER_H_

#include <stdio.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "state.h"

typedef struct Stepper_ {
  uint16_t pin1;
  uint16_t pin2;
  uint16_t pin3;
  uint16_t pin4;
  uint16_t led_pin;
  uint16_t steps_per_rav;
  uint16_t rpm;
} Stepper;

typedef struct Context_ {
  Stepper stepper;
  State state;
  int32_t steps;
  TaskHandle_t stepper_task_handle;

  // Create with xSemaphoreCreateBinary() by main. Used to allow one move at a
  // time. Server task should try to take the semaphore before preceeding.
  // Stepper task is only responsbile for giving it. Server task should return
  // if failed to take.
  SemaphoreHandle_t semaphore;
} Context;

esp_err_t start_stepper_task(Context* const context);

#endif  // STEPPER_H_