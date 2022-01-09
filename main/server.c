#include "server.h"

#include <fcntl.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "state.h"
#include "stepper.h"

#define TAG CONFIG_LOGGING_TAG
#define MAX_BUFFER (1024)

#define TAKE_SEMAPHORE_AND_GET_CONTEXT_OR_RETURN(req_expr)             \
  ({                                                                   \
    httpd_req_t *const req_ = (req_expr);                              \
    Context *context = (Context *)req_->user_ctx;                      \
    if (context == NULL) {                                             \
      httpd_resp_send_err(req_, HTTPD_500_INTERNAL_SERVER_ERROR,       \
                          "Context is NULL");                          \
      return ESP_FAIL;                                                 \
    }                                                                  \
    ESP_LOGI(TAG, "Take Semaphore");                                   \
    if (xSemaphoreTake(context->semaphore, (TickType_t)0) != pdTRUE) { \
      cJSON *root = cJSON_CreateObject();                              \
      cJSON_AddStringToObject(root, "msg", "Stepper is still moving"); \
      const char *json = cJSON_Print(root);                            \
      httpd_resp_sendstr(req_, json);                                  \
      free((void *)json);                                              \
      cJSON_Delete(root);                                              \
      return ESP_OK;                                                   \
    }                                                                  \
    context;                                                           \
  })

#define RETURN_IF_ERROR(status_expr, req_expr, http_error_code,       \
                        http_error_msg)                               \
  ({                                                                  \
    const esp_err_t status_code = (status_expr);                      \
    httpd_req_t *const req_ = (req_expr);                             \
    if ((status_code) != ESP_OK) {                                    \
      ESP_LOGE(TAG, http_error_msg);                                  \
      httpd_resp_send_err(req_, (http_error_code), (http_error_msg)); \
      return status_code;                                             \
    }                                                                 \
  })

#define RETURN_OK(req_expr)                     \
  ({                                            \
    httpd_req_t *const req_ = (req_expr);       \
    cJSON *root = cJSON_CreateObject();         \
    cJSON_AddStringToObject(root, "msg", "OK"); \
    const char *json = cJSON_Print(root);       \
    httpd_resp_sendstr((req_), json);           \
    free((void *)json);                         \
    cJSON_Delete(root);                         \
    return ESP_OK;                              \
  })

#define PARSE_OR_RETURN_ERROR(buf_expr)                                  \
  ({                                                                     \
    cJSON *root = cJSON_Parse((buf_expr));                               \
    if (root == NULL) {                                                  \
      ESP_LOGE(TAG, "Invalid input: %s", buf);                           \
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid input."); \
      return ESP_FAIL;                                                   \
    }                                                                    \
    root;                                                                \
  })

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
  ESP_LOGI(TAG, "Request: %s", buf);
  return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////
// Handlers
////////////////////////////////////////////////////////////////////////////////

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

static esp_err_t current_status_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  const Context *context = TAKE_SEMAPHORE_AND_GET_CONTEXT_OR_RETURN(req);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "max_steps", context->state.max_steps);
  cJSON_AddNumberToObject(root, "current_steps", context->state.current_step);
  const char *json = cJSON_Print(root);
  httpd_resp_sendstr(req, json);
  free((void *)json);
  cJSON_Delete(root);
  xSemaphoreGive(context->semaphore);
  return ESP_OK;
}

static esp_err_t unsafe_move_steps_put_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  char buf[MAX_BUFFER];
  RETURN_IF_ERROR(get_request_buffer(req, buf, MAX_BUFFER), req,
                  HTTPD_500_INTERNAL_SERVER_ERROR,
                  "Failed to get request body");

  cJSON *root = PARSE_OR_RETURN_ERROR(buf);
  int steps = cJSON_GetObjectItem(root, "steps")->valueint;
  cJSON_Delete(root);

  // Move the stepper.

  Context *const context = TAKE_SEMAPHORE_AND_GET_CONTEXT_OR_RETURN(req);
  context->steps = steps;
  xTaskNotifyGive(context->stepper_task_handle);
  RETURN_OK(req);
}

static esp_err_t move_to_fraction_put_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  char buf[MAX_BUFFER];
  RETURN_IF_ERROR(get_request_buffer(req, buf, MAX_BUFFER), req,
                  HTTPD_500_INTERNAL_SERVER_ERROR,
                  "Failed to get request body");

  cJSON *root = PARSE_OR_RETURN_ERROR(buf);
  const double fraction = cJSON_GetObjectItem(root, "fraction")->valuedouble;
  cJSON_Delete(root);

  if (fraction < 0 || fraction > 1) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fraction.");
    ESP_LOGE(TAG, "Fraction out of [0, 1] range. Got: %03f", fraction);
    return ESP_FAIL;
  }

  // Move the stepper.

  Context *const context = TAKE_SEMAPHORE_AND_GET_CONTEXT_OR_RETURN(req);
  if (context->state.max_steps < 0 || context->state.current_step < 0) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "State uninitialized.");
    ESP_LOGE(TAG, "State uninitialized");
    xSemaphoreGive(context->semaphore);
    return ESP_FAIL;
  }
  context->steps =
      (int)(fraction * context->state.max_steps) - context->state.current_step;
  xTaskNotifyGive(context->stepper_task_handle);
  RETURN_OK(req);
}

static esp_err_t reset_state_put_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  Context *const context = TAKE_SEMAPHORE_AND_GET_CONTEXT_OR_RETURN(req);

  context->state.max_steps = -1;
  context->state.current_step = -1;

  if (write_state_to_file(&context->state) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to persist state.");
    ESP_LOGE(TAG, "Failed to persist state.");
    xSemaphoreGive(context->semaphore);
    return ESP_FAIL;
  }
  xSemaphoreGive(context->semaphore);
  RETURN_OK(req);
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
  httpd_uri_t move_steps_put_uri = {.uri = "/move",
                                    .method = HTTP_PUT,
                                    .handler = move_to_fraction_put_handler,
                                    .user_ctx = context};
  httpd_register_uri_handler(server, &move_steps_put_uri);

  // Response:
  // {
  //    "max_steps": -1
  //    "current_step": -1
  // }
  httpd_uri_t reset_state_put_uri = {.uri = "/reset_state",
                                     .method = HTTP_PUT,
                                     .handler = reset_state_put_handler,
                                     .user_ctx = context};
  httpd_register_uri_handler(server, &reset_state_put_uri);

  return ESP_OK;
}