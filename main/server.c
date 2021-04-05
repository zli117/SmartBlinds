#include "server.h"

#include <fcntl.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "state.h"
#include "stepper.h"

#define TAG CONFIG_LOGGING_TAG
#define MAX_BUFFER (1024)

static esp_err_t system_info_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  cJSON *root = cJSON_CreateObject();
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  cJSON_AddStringToObject(root, "version", IDF_VER);
  cJSON_AddNumberToObject(root, "cores", chip_info.cores);
  const char *json = cJSON_Print(root);
  httpd_resp_sendstr(req, json);
  free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

static esp_err_t respond_with_state(httpd_req_t *req) {
  const State *state = get_state();
  if (state == NULL) {
    ESP_LOGE(TAG, "State pointer is NULL");
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "max_steps", state->max_steps);
  cJSON_AddNumberToObject(root, "current_steps", state->current_step);
  const char *json = cJSON_Print(root);
  httpd_resp_sendstr(req, json);
  free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

static esp_err_t current_status_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  return respond_with_state(req);
}

static esp_err_t get_request_buffer(httpd_req_t *req, char *buf,
                                    size_t max_len) {
  int total_len = req->content_len;
  int cur_len = 0;
  if (total_len >= max_len) {
    ESP_LOGE(TAG, "Request too large. Got length: %d, max: %d", total_len,
             max_len);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Content too long");
    return ESP_FAIL;
  }

  int received = 0;
  while (cur_len < total_len) {
    received = httpd_req_recv(req, buf + cur_len, total_len);
    if (received <= 0) {
      ESP_LOGE(TAG, "Received %d bytes.", received);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Failed to receive context");
      return ESP_FAIL;
    }
    cur_len += received;
  }
  buf[total_len] = '\0';
  return ESP_OK;
}

static esp_err_t unsafe_move_steps_put_handler(httpd_req_t *req) {
  char buf[MAX_BUFFER];
  esp_err_t err = get_request_buffer(req, buf, MAX_BUFFER);
  if (err != ESP_OK) {
    return err;
  }
  ESP_LOGI(TAG, "Request: %s", buf);

  cJSON *root = cJSON_Parse(buf);
  int steps = cJSON_GetObjectItem(root, "steps")->valueint;
  cJSON_Delete(root);

  // Move the stepper.

  Context *context = (Context *)req->user_ctx;

  if (context == NULL || context->stepper == NULL) {
    ESP_LOGE(TAG, "Context pointer 0x%0x, stepper pointer 0x%0x",
             (uint32_t)context,
             context == NULL ? 0 : (uint32_t)context->stepper);
    return ESP_FAIL;
  }

  State *state = get_mutable_state();
  if (state == NULL) {
    ESP_LOGE(TAG, "State pointer is NULL");
    return ESP_FAIL;
  }

  if (state->max_steps < 0 || state->current_step < 0) {
    ESP_LOGI(TAG, "Initialize state");
    state->max_steps = 0;
    state->current_step = 0;
  }

  stepper_step_block(context->stepper, steps);
  state->current_step += steps;
  if (state->current_step >= state->max_steps) {
    state->max_steps = state->current_step;
  }
  if (state->current_step < 0) {
    state->max_steps -= state->current_step;
    state->current_step = 0;
  }
  err = finish_mutation();
  if (err != ESP_OK) {
    return err;
  }

  httpd_resp_set_type(req, "application/json");
  return respond_with_state(req);
}

static esp_err_t move_to_fraction_put_handler(httpd_req_t *req) {
  char buf[MAX_BUFFER];
  esp_err_t err = get_request_buffer(req, buf, MAX_BUFFER);
  if (err != ESP_OK) {
    return err;
  }
  ESP_LOGI(TAG, "Request: %s", buf);

  cJSON *root = cJSON_Parse(buf);
  const double fraction = cJSON_GetObjectItem(root, "fraction")->valuedouble;
  cJSON_Delete(root);

  if (fraction < 0 || fraction > 1) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fraction.");
    ESP_LOGE(TAG, "Fraction out of [0, 1] range. Got: %03f", fraction);
    return ESP_FAIL;
  }

  // Move the stepper.

  Context *context = (Context *)req->user_ctx;

  if (context == NULL || context->stepper == NULL) {
    ESP_LOGE(TAG, "Context pointer 0x%0x, stepper pointer 0x%0x",
             (uint32_t)context,
             context == NULL ? 0 : (uint32_t)context->stepper);
    return ESP_FAIL;
  }

  State *state = get_mutable_state();
  if (state == NULL) {
    ESP_LOGE(TAG, "State pointer is NULL");
    return ESP_FAIL;
  }

  if (state->max_steps < 0 || state->current_step < 0) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "State uninitialized.");
    ESP_LOGE(TAG, "State uninitialized");
    return ESP_FAIL;
  }

  const int to_step = (int)(fraction * state->max_steps);

  stepper_step_block(context->stepper, to_step - state->current_step);
  state->current_step = to_step;

  err = finish_mutation();
  if (err != ESP_OK) {
    return err;
  }

  httpd_resp_set_type(req, "application/json");
  return respond_with_state(req);
}

esp_err_t start_restful_server(Context *context) {
  if (context == NULL) {
    ESP_LOGE(TAG, "Context pointer is NULL");
    return ESP_FAIL;
  }

  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  ESP_LOGI(TAG, "Starting HTTP server.");
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start server.");
    return ESP_FAIL;
  }

  // Response:
  // {
  //    "version": str  // Version string
  //    "cores": int  // Number of cores
  // }
  httpd_uri_t system_info_get_uri = {.uri = "/system_info",
                                     .method = HTTP_GET,
                                     .handler = system_info_get_handler};
  httpd_register_uri_handler(server, &system_info_get_uri);

  // Response:
  // {
  //    "max_steps": int  // Total number of steps possible
  //    "current_steps": int   // Current step number
  // }
  httpd_uri_t current_status_get_uri = {.uri = "/status",
                                        .method = HTTP_GET,
                                        .handler = current_status_get_handler,
                                        .user_ctx = context};
  httpd_register_uri_handler(server, &current_status_get_uri);

  // Request:
  // {
  //    "step": int  // Move this number of steps. Can be negative.
  // }
  // Response: Same as current_status_get_uri
  httpd_uri_t unsafe_move_steps_put_uri = {
      .uri = "/unsafe_move",
      .method = HTTP_PUT,
      .handler = unsafe_move_steps_put_handler,
      .user_ctx = context};
  httpd_register_uri_handler(server, &unsafe_move_steps_put_uri);

  // Request:
  // {
  //    "fraction": double  // Move to fraction of the max_steps.
  //                        // In range [0, 1].
  // }
  // Response: Same as current_status_get_uri
  httpd_uri_t move_steps_put_uri = {.uri = "/move",
                                    .method = HTTP_PUT,
                                    .handler = move_to_fraction_put_handler,
                                    .user_ctx = context};
  httpd_register_uri_handler(server, &move_steps_put_uri);

  return ESP_OK;
}