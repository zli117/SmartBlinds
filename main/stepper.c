#include "stepper.h"

#include <stdio.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG CONFIG_LOGGING_TAG
#define STACK_SIZE 2048

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

typedef struct TimerState_ {
  const uint16_t pin1;
  const uint16_t pin2;
  const uint16_t pin3;
  const uint16_t pin4;
  const uint16_t end;
  const int16_t direction;
  const TaskHandle_t stepper_task_handle;
  int64_t step;
} TimerState;

static const uint8_t pin_states[4] = {0b1010, 0b0110, 0b0101, 0b1001};

void timer_callback(void* parameter) {
  configASSERT(parameter != NULL);
  TimerState* const timer_state = (TimerState*)parameter;
  if (timer_state->step == timer_state->end) {
    vTaskNotifyGiveFromISR(timer_state->stepper_task_handle,
                           /*pxHigherPriorityTaskWoken=*/NULL);
    return;
  }
  const uint8_t pin_state = pin_states[timer_state->step % 4];
  gpio_set_level(timer_state->pin1, pin_state & 0b1000);
  gpio_set_level(timer_state->pin2, pin_state & 0b0100);
  gpio_set_level(timer_state->pin3, pin_state & 0b0010);
  gpio_set_level(timer_state->pin4, pin_state & 0b0001);
  timer_state->step += timer_state->direction;
}

void stepper_task(void* parameter) {
  configASSERT(parameter != NULL);
  Context* const context = (Context*)parameter;
  configASSERT(context->semaphore != NULL);

  while (true) {
    xTaskNotifyWait(/*do not clear notification on enter*/ 0x00,
                    /*clear notification on exit*/ ULONG_MAX,
                    /*pulNotificationValue=*/NULL, portMAX_DELAY);

    // Delete state so if it somehow fails in the middle, the state won't be
    // inconsistent.
    delete_state_file();

    const int64_t steps_per_min =
        (context->stepper.steps_per_rav * context->stepper.rpm);
    const int64_t delay_us = (60L * 1e6L + steps_per_min - 1) / steps_per_min;
    const int32_t steps = context->steps;
    TimerState timer_state = {
        .pin1 = context->stepper.pin1,
        .pin2 = context->stepper.pin2,
        .pin3 = context->stepper.pin3,
        .pin4 = context->stepper.pin4,
        .end = steps > 0 ? steps : 0,
        .direction = steps > 0 ? 1 : -1,
        .stepper_task_handle = context->stepper_task_handle,
        .step = steps > 0 ? 0 : -steps};

    // Start timer for stepping

    esp_timer_create_args_t timer_args = {.callback = &timer_callback,
                                          .arg = &timer_state,
                                          .dispatch_method = ESP_TIMER_TASK,
                                          .name = "Stepper Timer"};
    esp_timer_handle_t timer_handle;
    configASSERT(esp_timer_create(&timer_args, &timer_handle) == ESP_OK);
    configASSERT(esp_timer_start_periodic(timer_handle, delay_us) == ESP_OK);

    // Sleep to wait for finish
    xTaskNotifyWait(
        /*do not clear notification on enter*/ 0x00,
        /*clear notification on exit*/ ULONG_MAX,
        /*pulNotificationValue=*/NULL, portMAX_DELAY);

    configASSERT(esp_timer_stop(timer_handle) == ESP_OK);
    configASSERT(esp_timer_delete(timer_handle) == ESP_OK);

    gpio_set_level(context->stepper.pin1, 0);
    gpio_set_level(context->stepper.pin2, 0);
    gpio_set_level(context->stepper.pin3, 0);
    gpio_set_level(context->stepper.pin4, 0);

    // Update state
    State* state = &context->state;
    if (state->max_steps < 0 || state->current_step < 0) {
      ESP_LOGI(TAG, "Initialize state");
      state->max_steps = 0;
      state->current_step = 0;
    }
    state->current_step += steps;
    if (state->current_step >= state->max_steps) {
      state->max_steps = state->current_step;
    }
    if (state->current_step < 0) {
      state->max_steps -= state->current_step;
      state->current_step = 0;
    }
    write_state_to_file(&context->state);

    xSemaphoreGive(context->semaphore);
  }
}

esp_err_t start_stepper_task(Context* const context) {
  if (context == NULL) {
    return ESP_FAIL;
  }
  stepper_init(&context->stepper);
  xTaskCreate(&stepper_task, "stepper_task", STACK_SIZE, context,
              configMAX_PRIORITIES - 2, &context->stepper_task_handle);
  if (context->stepper_task_handle == NULL) {
    ESP_LOGI(TAG, "Failed to create stepper task");
    return ESP_FAIL;
  }
  return ESP_OK;
}
