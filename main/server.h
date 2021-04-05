#ifndef SERVER_H_
#define SERVER_H_

#include "esp_err.h"
#include "state.h"
#include "stepper.h"

typedef struct Context_ {
  Stepper *stepper;
} Context;

esp_err_t start_restful_server(Context *context);

#endif  // SERVER_H_