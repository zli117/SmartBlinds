#include "stepper.h"

#include <stdio.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_FREERTOS_HZ != 1000
  #error CONFIG_FREERTOS_HZ (FreeRTOS tick frequency) has to be exactly 1000
#endif 

void stepper_init(const Stepper* stepper) {
  gpio_pad_select_gpio(stepper->pin1);
  gpio_pad_select_gpio(stepper->pin2);
  gpio_pad_select_gpio(stepper->pin3);
  gpio_pad_select_gpio(stepper->pin4);
  gpio_set_direction(stepper->pin1, GPIO_MODE_OUTPUT);
  gpio_set_direction(stepper->pin2, GPIO_MODE_OUTPUT);
  gpio_set_direction(stepper->pin3, GPIO_MODE_OUTPUT);
  gpio_set_direction(stepper->pin4, GPIO_MODE_OUTPUT);
}

void stepper_step_block(const Stepper* stepper, int32_t steps) {
  const int64_t steps_per_min = (stepper->steps_per_rav * stepper->rpm);
  const int64_t delay_ms = (60L * 1e3L + steps_per_min - 1) / steps_per_min;
  const int32_t end = steps > 0 ? steps : 0;
  const int32_t direction = steps > 0 ? 1 : -1;
  int32_t step = steps > 0 ? 0 : -steps;
  const uint8_t pin_states[4] = {0b1010, 0b0110, 0b0101, 0b1001};
  while (step != end) {
    const uint8_t pin_state = pin_states[step % 4];
    gpio_set_level(stepper->pin1, pin_state & 0b1000);
    gpio_set_level(stepper->pin2, pin_state & 0b0100);
    gpio_set_level(stepper->pin3, pin_state & 0b0010);
    gpio_set_level(stepper->pin4, pin_state & 0b0001);
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    step += direction;
  }
  gpio_set_level(stepper->pin1, 0);
  gpio_set_level(stepper->pin2, 0);
  gpio_set_level(stepper->pin3, 0);
  gpio_set_level(stepper->pin4, 0);
}