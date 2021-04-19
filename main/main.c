/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "server.h"
#include "state.h"
#include "stepper.h"
#include "wifi.h"

#define TAG CONFIG_LOGGING_TAG

#define PRINT_ERROR_OR_SUCCESS(status_expr, success_msg, failed_msg) \
  ({                                                                 \
    const esp_err_t err = (status_expr);                             \
    if (err != ESP_OK) {                                             \
      ESP_LOGE(TAG, failed_msg);                                     \
    }                                                                \
    ESP_LOGI(TAG, success_msg);                                      \
  })

static Context context = {.stepper = {.pin1 = CONFIG_GPIO_1,
                                      .pin2 = CONFIG_GPIO_2,
                                      .pin3 = CONFIG_GPIO_3,
                                      .pin4 = CONFIG_GPIO_4,
                                      .led_pin = CONFIG_LED_GPIO,
                                      .steps_per_rav = CONFIG_STEPS_PER_REV,
                                      .rpm = CONFIG_RPM},
                          .state = {.max_steps = -1, .current_step = -1}};

void app_main(void) {
  context.semaphore = xSemaphoreCreateBinary();
  configASSERT(context.semaphore != NULL);
  xSemaphoreGive(context.semaphore);

  PRINT_ERROR_OR_SUCCESS(wifi_init_sta(), "Initialized Wifi.",
                         "Failed to initialize Wifi.");
  PRINT_ERROR_OR_SUCCESS(init_storage_and_state(&context.state),
                         "Initialized state.", "Init state failed");
  PRINT_ERROR_OR_SUCCESS(start_stepper_task(&context), "Stepper task started",
                         "Failed to start stepper task");
  PRINT_ERROR_OR_SUCCESS(start_restful_server(&context), "Server started",
                         "Failed to start server.");
}
