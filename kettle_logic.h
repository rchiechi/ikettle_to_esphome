#pragma once
#include "esphome.h"

class KettleLogic {
  private:
    // Internal State
    unsigned long hold_start_time = 0;
    float last_temp = 0.0;
    bool pulse_up = true;
    
    // RENAMED: To avoid conflict with id(kettle_present)
    bool has_kettle = false; 
    
    // Constants
    const int LOOP_DURATION = 1000;
    const float HYST = 2.0;
    const float MAX_TEMP = 108.0;
    const float MIN_TEMP = 1.0;
    const float MAX_RATE = 8.0;
    
    // FIXED: Use enum for Switch Cases (Compile-time constants)
    enum State {
      STATE_OFF = 0,
      STATE_ERROR = 1,
      STATE_BOILING = 2,
      STATE_WARMING = 3,
      STATE_NOKETTLE = 4,
      STATE_DONE = 5
    };

    // ----------------------------------------------------------------
    // HARDWARE HELPERS
    // ----------------------------------------------------------------

    void set_heater(bool on){
      // Use internal 'has_kettle' variable
      if (on && has_kettle) {
        id(relay_hardware).turn_on();
      }
      else {
        id(relay_hardware).turn_off();
      }
    }

    void set_led(int state){
      auto call = id(kettle_led).turn_on();
      call.set_state(true);    

      if (state == STATE_OFF || state == STATE_DONE) {
        call.set_state(false);
        call.set_transition_length(0); 
      }
      else if (state == STATE_ERROR || state == STATE_NOKETTLE) {
        call.set_brightness(pulse_up ? 1.0 : 0.0);
        call.set_transition_length(0); 
      }
      else if (state == STATE_BOILING) {
        call.set_brightness(1.0);
        call.set_transition_length(LOOP_DURATION); 
      }
      else if (state == STATE_WARMING) {
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
          case STATE_OFF:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            
            // Note: We use id(kettle_present) here (The ESPHome Sensor)
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            
            if (id(kettle_active).state) id(kettle_active).turn_off();
            id(fault_status).publish_state("Idle");
            break;

          case STATE_BOILING:
            if (!id(boiling_status).state) id(boiling_status).publish_state(true);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            id(fault_status).publish_state("Boiling");
            break;

          case STATE_WARMING:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (!id(keeping_warm_status).state) id(keeping_warm_status).publish_state(true);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            id(fault_status).publish_state("Keeping Warm");
            break;

          case STATE_DONE:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            
            if (id(kettle_active).state) id(kettle_active).turn_off();
            id(fault_status).publish_state("Target Reached");
            break;

          case STATE_NOKETTLE:
            set_heater(false); 
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (id(kettle_present).state) id(kettle_present).publish_state(false);
            
            if (id(kettle_active).state) id(kettle_active).turn_off();
            id(fault_status).publish_state("Kettle Missing");
            break;

          case STATE_ERROR:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            
            if (id(kettle_active).state) id(kettle_active).turn_off();
            // Don't overwrite specific error message
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
      
      if (isnan(current)) {
         set_heater(false);
         return;
      }

      float rate = 0.0;
      if (last_temp > 1.0) rate = current - last_temp;
      last_temp = current;

      // 2. DETERMINE SYSTEM STATE
      int state = STATE_OFF; 

      // --- Kettle presence ---
      if (current < MIN_TEMP) {
        has_kettle = false; // Internal tracking
        state = STATE_NOKETTLE;
        set_heater(false); 
      } else {
        has_kettle = true;
      }
      
      // --- Safety Checks ---
      if (active && rate > MAX_RATE) {
        state = STATE_ERROR;
        shutdown("Error: Dry Boil");
      }
      else if (current > MAX_TEMP) {
        state = STATE_ERROR;
        shutdown("Error: Overheat");
      }

      // 3. SET KETTLE STATE (Control Logic)

      if (!active && state != STATE_ERROR) {
        // IDLE
        set_heater(false);
        hold_start_time = 0;
        if (state != STATE_NOKETTLE) state = STATE_OFF;
      }
      else if (state != STATE_ERROR) {
        // ACTIVE
        if (hold_start_time == 0) {
          // --- HEATING ---
          state = STATE_BOILING;

          if (current < target && has_kettle) {
             if (current < (target - HYST) && !id(relay_hardware).state) {
                set_heater(true);
             }
          } 
          else if (!has_kettle) {
            state = STATE_NOKETTLE;
            set_heater(false);
          } 
          else {
             set_heater(false);
             hold_start_time = millis();
             state = STATE_DONE;
          }
        }
        else {
          // --- KEEP WARM ---
          unsigned long elapsed = millis() - hold_start_time;
          unsigned long limit = keep_warm * 60 * 1000;

          if (limit == 0 || elapsed > limit) {
             state = STATE_OFF; 
          } 
          else if (has_kettle) {
             state = STATE_WARMING;
             if (current < (target - HYST) && !id(relay_hardware).state) set_heater(true);
             else if (current >= target && id(relay_hardware).state) set_heater(false);
          } 
          else {
             state = STATE_NOKETTLE;
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