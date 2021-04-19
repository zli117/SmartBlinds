#include "state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#define TAG CONFIG_LOGGING_TAG
#define BASE_PATH CONFIG_FLASH_PARTITION_PATH
#define FILE_PATH CONFIG_STATE_FILE_PATH
#define PARTITION_LABEL "storage"

static wl_handle_t wl_handle = WL_INVALID_HANDLE;

esp_err_t load_state_from_file(State* state) {
  FILE* file = fopen(FILE_PATH, "rb");
  if (file == NULL) {
    ESP_LOGE(TAG, "Failed to open file: %s", FILE_PATH);
    return errno;
  }

  State temp;
  if (fread(&temp, sizeof(temp), /*nmemb*/ 1, file) != 1 && ferror(file) != 0) {
    ESP_LOGE(TAG, "Failed to read file");
    return ESP_FAIL;
  }

  state->max_steps = temp.max_steps;
  state->current_step = temp.current_step;
  return ESP_OK;
}

esp_err_t init_storage_and_state(State* state) {
  ESP_LOGI(TAG, "Mounting FAT filesystem.");
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 4,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE};
  esp_err_t err = esp_vfs_fat_spiflash_mount(BASE_PATH, PARTITION_LABEL,
                                             &mount_config, &wl_handle);
  if (err != ESP_OK) {
    return err;
  }

  if (access(FILE_PATH, F_OK) != 0) {
    FILE* file = fopen(FILE_PATH, "wb");
    if (file == NULL) {
      ESP_LOGE(TAG, "Failed to create file: %s", FILE_PATH);
      return errno;
    }
    if (fclose(file) != 0) {
      return errno;
    }
    state->max_steps = -1;
    state->current_step = -1;
    return ESP_OK;
  } else {
    if (({
          err = load_state_from_file(state);
          err != ESP_OK;
        })) {
      return err;
    }
    if (state->max_steps < 0 || state->current_step < 0) {
      ESP_LOGE(TAG,
               "Unexpceted state from file: max_steps = %d, current_step = %d",
               state->max_steps, state->current_step);
      state->max_steps = -1;
      state->current_step = -1;
    }
    return ESP_OK;
  }
}

esp_err_t delete_state_file() {
  if (remove(FILE_PATH) != 0) {
    ESP_LOGE(TAG, "Failed to remove file: %s", FILE_PATH);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t write_state_to_file(const State* state) {
  FILE* file = fopen(FILE_PATH, "wb");
  if (file == NULL) {
    ESP_LOGE(TAG, "Failed to open file for write: %s", FILE_PATH);
    return errno;
  }
  if (fwrite(state, sizeof(State), /*nmemb=*/1, file) != 1) {
    ESP_LOGE(TAG, "Failed to write");
    return ESP_FAIL;
  }
  if (fclose(file) != 0) {
    return errno;
  }
  return ESP_OK;
}
