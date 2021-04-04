#ifndef STATE_H_
#define STATE_H_

#include "esp_err.h"
#include "esp_system.h"

typedef struct State_ {
  int16_t max_steps;
  int16_t current_steps;
} State;

esp_err_t init_state();

State* get_mutable_state();

esp_err_t finish_mutation();

#endif  // STATE_H_