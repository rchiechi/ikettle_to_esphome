#pragma once
#include "esphome.h"

class KettleLogic {
  private:
    // Internal State
    unsigned long hold_start_time = 0;
    float last_temp = 0.0;
    
    // We only track this to prevent the "Slow Blink" effect 
    // from resetting its timer every 1s (which makes it look jittery).
    int last_led_mode = -1; 

    const float HYST = 2.0;
    const float MAX_TEMP = 108.0;

  public:
    void loop() {
      // ----------------------------------------------------------
      // 1. GATHER INPUTS
      // ----------------------------------------------------------
      bool active = id(kettle_active).state;
      float current = id(water_temp).state;
      float target = id(target_temp).state;
      float keep_warm = id(keep_warm_mins).state;

      // NAN Guard (skip if sensor isn't ready)
      if (isnan(current)) return;

      // Rate Calc
      float rate = 0.0;
      if (last_temp > 1.0) rate = current - last_temp;
      last_temp = current;

      // ----------------------------------------------------------
      // 2. SAFETY CHECKS (Updates Fault Status)
      // ----------------------------------------------------------
      bool error = false;

      if (current < 1.0) {
        shutdown("Error: Kettle Missing");
        error = true;
      } 
      else if (active && rate > 8.0) {
        shutdown("Error: Dry Boil");
        error = true;
      }
      else if (current > MAX_TEMP) {
        shutdown("Error: Overheat");
        error = true;
      }

      // ----------------------------------------------------------
      // 3. THERMOSTAT LOGIC (Updates Binary Sensors & Relay)
      // ----------------------------------------------------------
      if (!error) {
        if (!active) {
          // USER TURNED OFF
          if (id(relay_hardware).state) id(relay_hardware).turn_off();
          hold_start_time = 0;
          
          // Force sensors off immediately so LED logic picks it up below
          if (id(boiling_status).state) id(boiling_status).publish_state(false);
          if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
        }
        else {
          // USER TURNED ON
          if (hold_start_time == 0) {
            // PHASE 1: HEATING
            if (!id(boiling_status).state) id(boiling_status).publish_state(true);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);

            if (current < target) {
              if (current < (target - HYST) && !id(relay_hardware).state) {
                 id(relay_hardware).turn_on();
                 id(fault_status).publish_state("Heating");
              }
            } else {
              id(relay_hardware).turn_off();
              hold_start_time = millis(); // Mark the transition
              id(fault_status).publish_state("Target Reached");
            }
          }
          else {
            // PHASE 2: KEEP WARM
            unsigned long elapsed = millis() - hold_start_time;
            unsigned long limit = keep_warm * 60 * 1000;

            if (limit == 0 || elapsed > limit) {
              // Time's up
              id(kettle_active).turn_off();
              id(fault_status).publish_state("Done");
            } else {
              // Maintain
              if (id(boiling_status).state) id(boiling_status).publish_state(false);
              if (!id(keeping_warm_status).state) id(keeping_warm_status).publish_state(true);
              
              id(fault_status).publish_state("Keeping Warm");
              if (current < (target - HYST) && !id(relay_hardware).state) id(relay_hardware).turn_on();
              else if (current >= target && id(relay_hardware).state) id(relay_hardware).turn_off();
            }
          }
        }
      }

      // ----------------------------------------------------------
      // 4. LED LOGIC
      // ----------------------------------------------------------
      int led_mode = 0; // Default Off

      // Check 1: Error?
      if (error) {
        led_mode = 1; // Fast Blink
      }
      // Check 2: Boiling?
      else if (id(boiling_status).state) {
        led_mode = 2; // Solid
      }
      // Check 3: Keeping Warm?
      else if (id(keeping_warm_status).state) {
        led_mode = 3; // Slow Pulse
      }
      // Else: Off (led_mode remains 0)

      update_led(led_mode);
    }

  private:
    void shutdown(const char* msg) {
      id(relay_hardware).turn_off();
      id(kettle_active).turn_off();
      id(fault_status).publish_state(msg);
      // Clear binary sensors so LED turns off/blinks error correctly
      id(boiling_status).publish_state(false);
      id(keeping_warm_status).publish_state(false);
    }

    void update_led(int mode) {
      if (mode == last_led_mode) return; // Prevent effect reset loop

      auto call = id(kettle_led).turn_on();
      
      if (mode == 0) {
        call.set_state(false);
        call.set_effect("None");
        call.set_transition_length(0); // Snap Off
      } 
      else {
        call.set_state(true);
        call.set_brightness(1.0);
        call.set_transition_length(0); // Snap On
        
        if (mode == 1) call.set_effect("Fast Blink");
        else if (mode == 2) call.set_effect("None");
        else if (mode == 3) call.set_effect("Slow Blink");
      }
      call.perform();
      last_led_mode = mode;
    }
};

KettleLogic kettle_logic;