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
#include "server.h"
#include "state.h"
#include "stepper.h"
#include "wifi.h"

#define TAG CONFIG_LOGGING_TAG

static Stepper stepper = {.pin1 = CONFIG_GPIO_1,
                          .pin2 = CONFIG_GPIO_2,
                          .pin3 = CONFIG_GPIO_3,
                          .pin4 = CONFIG_GPIO_4,
                          .steps_per_rav = CONFIG_STEPS_PER_REV,
                          .rpm = CONFIG_RPM};

static Context server_context = {.stepper = &stepper};

void app_main(void) {
  wifi_init_sta();
  ESP_LOGI(TAG, "Initialize Wifi");

  stepper_init(&stepper);
  ESP_LOGI(TAG, "Initialized stepper.");

  esp_err_t err;

  err = init_state();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Init state failed: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "Initialized state.");


  err = start_restful_server(&server_context);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start server.");
    return;
  }
  ESP_LOGI(TAG, "Server started");
}
