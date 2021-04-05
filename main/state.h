#ifndef STATE_H_
#define STATE_H_

#include "esp_err.h"
#include "esp_system.h"

typedef struct State_ {
  int16_t max_steps;  // Not inclusive
  int16_t current_step;
} State;

esp_err_t init_state();

State* get_mutable_state();

const State* get_state();

esp_err_t finish_mutation();

#endif  // STATE_H_