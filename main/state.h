#ifndef STATE_H_
#define STATE_H_

#include "esp_err.h"
#include "esp_system.h"

typedef struct State_ {
  int16_t max_steps;  // Not inclusive
  int16_t current_step;
} State;

esp_err_t load_state_from_file(State* state);

esp_err_t init_storage_and_state(State* state);

esp_err_t delete_state_file(); 

esp_err_t write_state_to_file(const State* state);

#endif  // STATE_H_