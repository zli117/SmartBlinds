#ifndef STEPPER_H_
#define STEPPER_H_

#include <stdio.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct Stepper_ {
  uint16_t pin1;
  uint16_t pin2;
  uint16_t pin3;
  uint16_t pin4;
  uint16_t steps_per_rav;
  uint16_t rpm;
} Stepper;

// Initializes GPIOs with the specified struct.
void stepper_init(const Stepper* stepper);

// Steps with specified rpm and pins. If steps is positive, do clock-wise.
// Otherwise counter-clockwise. If steps is 0, then sets all pins to LOW only.
// Note that due to integer division, the actual RPM could be slightly lower
// than specified.
void stepper_step_block(const Stepper* stepper, int32_t steps);

#endif  // STEPPER_H_