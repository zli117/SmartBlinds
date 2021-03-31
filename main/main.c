/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <sys/param.h>

#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "stepper.h"

#define TAG CONFIG_LOGGING_TAG
#define BASE_PATH CONFIG_FLASH_PARTITION_PATH
#define FILE_NAME "state.bin"

static wl_handle_t wl_handle = WL_INVALID_HANDLE;
static Stepper stepper = {.pin1 = CONFIG_GPIO_1,
                          .pin2 = CONFIG_GPIO_2,
                          .pin3 = CONFIG_GPIO_3,
                          .pin4 = CONFIG_GPIO_4,
                          .steps_per_rav = CONFIG_STEPS_PER_REV,
                          .rpm = CONFIG_RPM};

void app_main(void) {
  ESP_LOGI(TAG, "Mounting FAT filesystem");
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 4,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE};
  esp_err_t err = esp_vfs_fat_spiflash_mount(BASE_PATH, "storage",
                                             &mount_config, &wl_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
    return;
  }

  stepper_init(&stepper);
}
