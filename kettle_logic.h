#pragma once
#include "esphome.h"

class KettleLogic {
  private:
    // State Tracking
    unsigned long hold_start_time = 0;
    float last_temp = 0.0;
    int last_led_mode = -1;
    bool error_detected = false;

    // Constants
    const float HYST = 2.0;
    const float MAX_TEMP = 108.0;
    const float DRY_BOIL_RATE = 8.0;

    void set_led(int mode) {
      if (mode == last_led_mode) return;
      
      auto call = id(kettle_led).turn_on();
      if (mode == 0) {
        id(kettle_led).turn_off();
      } else {
        if (mode == 1) call.set_effect("Fast Blink");
        else if (mode == 2) call.set_effect("None"); // Solid
        else if (mode == 3) call.set_effect("Slow Blink");
        call.perform();
      }
      last_led_mode = mode;
    }

  public:
    void loop() {
      // 1. GATHER INPUTS
      bool active = id(kettle_active).state;
      float current = id(water_temp).state;
      float target = id(target_temp).state;
      float keep_warm = id(keep_warm_mins).state;
      int target_led_mode = 0;

      // NAN Guard
      if (isnan(current)) return;

      // Rate Calculation
      float rate = 0.0;
      if (last_temp > 1.0) rate = current - last_temp;
      last_temp = current;

      // 2. SAFETY CHECKS
      error_detected = false;
      
      // Sensor missing
      if (current < 1.0) {
        if (active || id(relay_hardware).state) {
          trigger_safety_shutdown("Error: Kettle Missing");
        }
        error_detected = true;
      }
      // Dry Boil
      else if (active && rate > DRY_BOIL_RATE) {
        ESP_LOGE("safety", "Dry Boil! Rate: %.2f", rate);
        trigger_safety_shutdown("Error: Dry Boil");
        error_detected = true;
      }
      // Overheat
      else if (current > MAX_TEMP) {
        trigger_safety_shutdown("Error: Overheat");
        error_detected = true;
      }

      // 3. DECISION LOGIC
      if (error_detected) {
        target_led_mode = 1; // Fast Blink
        reset_ui_flags();
      } 
      else if (!active) {
        process_idle_state();
        target_led_mode = 0;
      } 
      else {
        target_led_mode = process_active_state(current, target, keep_warm);
      }

      // 4. OUTPUTS
      set_led(target_led_mode);
    }

    // Helpers
    void trigger_safety_shutdown(const char* msg) {
      id(relay_hardware).turn_off();
      id(kettle_active).turn_off();
      id(fault_status).publish_state(msg);
      hold_start_time = 0;
    }

    void reset_ui_flags() {
      id(boiling_status).publish_state(false);
      id(keeping_warm_status).publish_state(false);
    }

    void process_idle_state() {
      if (id(relay_hardware).state) id(relay_hardware).turn_off();
      hold_start_time = 0;
      reset_ui_flags();
    }

    int process_active_state(float current, float target, float keep_warm) {
      // Mode A: Heating
      if (hold_start_time == 0) {
        if (!id(boiling_status).state) id(boiling_status).publish_state(true);
        if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
        
        if (current < target) {
          if (current < (target - HYST) && !id(relay_hardware).state) {
             id(relay_hardware).turn_on();
             id(fault_status).publish_state("Heating");
          }
        } else {
          id(relay_hardware).turn_off();
          hold_start_time = millis();
          id(fault_status).publish_state("Target Reached");
        }
        return 2; // Solid LED
      }
      // Mode B: Keep Warm
      else {
        unsigned long elapsed = millis() - hold_start_time;
        unsigned long limit = keep_warm * 60 * 1000;
        
        if (limit == 0 || elapsed > limit) {
          id(kettle_active).turn_off();
          id(fault_status).publish_state("Done");
          hold_start_time = 0;
          return 0; // LED Off
        } else {
          if (id(boiling_status).state) id(boiling_status).publish_state(false);
          if (!id(keeping_warm_status).state) id(keeping_warm_status).publish_state(true);
          
          id(fault_status).publish_state("Keeping Warm");
          
          // Simple Thermostat
          if (current < (target - HYST) && !id(relay_hardware).state) id(relay_hardware).turn_on();
          else if (current >= target && id(relay_hardware).state) id(relay_hardware).turn_off();
          
          return 3; // Slow Blink LED
        }
      }
    }
};

KettleLogic kettle_logic;