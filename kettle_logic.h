#pragma once
#include "esphome.h"

class KettleLogic {
  private:
    // Internal State
    unsigned long hold_start_time = 0;
    float last_temp = 0.0;
    
    // Pulse State Tracker (flips every second for breathing effect)
    bool pulse_up = true;
    
    // Track kettle presence
    bool kettle_present = false;
    
    // Constants
    const int LOOP_DURATION = 1000; // Loop duration in ms
    const float HYST = 2.0;
    const float MAX_TEMP = 108.0;
    const float MIN_TEMP = 1.0;
    const float MAX_RATE = 8.0;
    
    // States
    const int OFF = 0;
    const int ERROR = 1;
    const int BOILING = 2;
    const int WARMING = 3;
    const int NOKETTLE = 4;
    const int DONE = 5;

    // ----------------------------------------------------------------
    // HARDWARE HELPERS (Must be defined first)
    // ----------------------------------------------------------------

    // Final safety gate for the relay
    void set_heater(bool on){
      if (on && kettle_present) {
        id(relay_hardware).turn_on();
      }
      else {
        id(relay_hardware).turn_off();
      }
    }

    void set_led(int state){
      auto call = id(kettle_led).turn_on();
      call.set_state(true);    // Default to ON

      if (state == OFF || state == DONE) {
        call.set_state(false);
        call.set_transition_length(0); 
      }
      else if (state == ERROR || state == NOKETTLE) {
        call.set_brightness(pulse_up ? 1.0 : 0.0);
        call.set_transition_length(0); 
      }
      else if (state == BOILING) {
        call.set_brightness(1.0);
        call.set_transition_length(LOOP_DURATION); 
      }
      else if (state == WARMING) {
        call.set_brightness(pulse_up ? 1.0 : 0.2);
        call.set_transition_length(LOOP_DURATION);
      }
      call.perform();
      pulse_up = !pulse_up;
    }

    // ----------------------------------------------------------------
    // UI & STATE SYNCHRONIZATION
    // ----------------------------------------------------------------
    void sync_ui(int state){
        switch (state) {
          case OFF:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            
            // Side Effect: Ensure Switch is OFF
            if (id(kettle_active).state) id(kettle_active).turn_off();
            
            id(fault_status).publish_state("Idle");
            break;

          case BOILING:
            if (!id(boiling_status).state) id(boiling_status).publish_state(true);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            id(fault_status).publish_state("Boiling");
            break;

          case WARMING:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (!id(keeping_warm_status).state) id(keeping_warm_status).publish_state(true);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            id(fault_status).publish_state("Keeping Warm");
            break;

          case DONE:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            
            // Side Effect: Ensure Switch is OFF
            if (id(kettle_active).state) id(kettle_active).turn_off();
            
            id(fault_status).publish_state("Target Reached");
            break;

          case NOKETTLE:
            set_heater(false); // Redundant safety
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (id(kettle_present).state) id(kettle_present).publish_state(false);
            
            // Side Effect: Ensure Switch is OFF (Abort logic)
            if (id(kettle_active).state) id(kettle_active).turn_off();
            
            id(fault_status).publish_state("Kettle Missing");
            break;

          case ERROR:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            
            // Side Effect: Ensure Switch is OFF
            if (id(kettle_active).state) id(kettle_active).turn_off();
            
            // Note: We do not publish generic "Error" text here so we don't 
            // overwrite specific messages like "Dry Boil" set by the loop.
            id(fault_status).publish_state("Error");
            break;

          default:
            id(fault_status).publish_state("Internal error tracking state");
            if (id(kettle_active).state) id(kettle_active).turn_off();
            break;
        }    
    }

    void shutdown(const char* msg) {
      set_heater(false);
      // Turn off active switch immediately
      if (id(kettle_active).state) id(kettle_active).turn_off();
      id(fault_status).publish_state(msg);
    }
    
  public:
    void loop() {
      // 1. GATHER INPUTS
      bool active = id(kettle_active).state;
      float current = id(water_temp).state;
      float target = id(target_temp).state;
      float keep_warm = id(keep_warm_mins).state;
      
      // NAN Guard (Fail Safe)
      if (isnan(current)) {
         set_heater(false);
         return;
      }

      // Rate Calculation
      float rate = 0.0;
      if (last_temp > 1.0) rate = current - last_temp;
      last_temp = current;

      // 2. DETERMINE SYSTEM STATE
      int state = OFF; 

      // --- Kettle presence ---
      if (current < MIN_TEMP) {
        kettle_present = false;
        state = NOKETTLE;
        set_heater(false); // Immediate safety
      } else {
        kettle_present = true;
      }
      
      // --- Safety Checks ---
      if (active && rate > MAX_RATE) {
        state = ERROR;
        shutdown("Error: Dry Boil");
      }
      else if (current > MAX_TEMP) {
        state = ERROR;
        shutdown("Error: Overheat");
      }

      // 3. SET KETTLE STATE (Control Logic)

      if (!active && state != ERROR) {
        // IDLE
        set_heater(false);
        hold_start_time = 0;
        // Keep NOKETTLE status if missing, otherwise OFF
        if (state != NOKETTLE) state = OFF;
      }
      else if (state != ERROR) {
        // ACTIVE
        if (hold_start_time == 0) {
          // --- HEATING ---
          state = BOILING;

          if (current < target && kettle_present) {
             if (current < (target - HYST) && !id(relay_hardware).state) {
                set_heater(true);
             }
          } 
          else if (!kettle_present) {
            state = NOKETTLE;
            set_heater(false);
          } 
          else {
             set_heater(false);
             hold_start_time = millis();
             state = DONE;
          }
        }
        else {
          // --- KEEP WARM ---
          unsigned long elapsed = millis() - hold_start_time;
          unsigned long limit = keep_warm * 60 * 1000;

          if (limit == 0 || elapsed > limit) {
             // Timer Finished
             state = OFF; // sync_ui will turn off kettle_active
          } 
          else if (kettle_present) {
             // Maintaining
             state = WARMING;
             if (current < (target - HYST) && !id(relay_hardware).state) set_heater(true);
             else if (current >= target && id(relay_hardware).state) set_heater(false);
          } 
          else {
             // Kettle removed during keep warm
             state = NOKETTLE;
             set_heater(false);
          }
        }
      }

      // 4. APPLY OUTPUTS
      sync_ui(state);
      set_led(state);
    }
};

KettleLogic kettle_logic;