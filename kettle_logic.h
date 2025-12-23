#pragma once
#include "esphome.h"

class KettleLogic {
  private:
    // Internal State
    unsigned long hold_start_time = 0;
    unsigned long heating_start_time = 0;    
    unsigned long last_calc_time = 0;
    float last_temp = 0.0;
    float current_rate = 0.0; // The smoothed rate (deg/sec)
    
    // Animation Helpers
    unsigned long led_start_time = 0;
    bool pulse_up = true;
    
    // Track kettle presence
    bool has_kettle = false; 
    
    // Constants
    const float HYST = 3.0;
    const float THERMAL_LAG = 3.0;
    const float MAX_TEMP = 108.0;
    const float MIN_TEMP = 1.0;
    const float MAX_RATE = 5.0;
    
    int LAST_STATE = 0;
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
      if (on && has_kettle) {
        if (!id(relay_hardware).state) {
           id(relay_hardware).turn_on();
           heating_start_time = millis(); 
        }
      }
      else {
        if (id(relay_hardware).state) {
           id(relay_hardware).turn_off();
           heating_start_time = 0;
        }
      }
    }

    void set_led(int state, int loop_duration){
      auto call = id(kettle_led).turn_on();
      call.set_state(true);    

      // Dynamic Fast Blink Speed Calculation
      int error_blink_period = (loop_duration > 500) ? loop_duration : 500;
      unsigned long now = millis();

      bool execute = false;

      switch (state) {
        // --- STATIC STATES ---
        case STATE_OFF:
        case STATE_DONE:
          // Off
          call.set_state(false);
          call.set_transition_length(0);
          led_start_time = 0;
          if (state != LAST_STATE) execute = true;
          break;

        case STATE_BOILING:
          // Solid on
          call.set_brightness(1.0);
          call.set_transition_length(std::max(1000, loop_duration)); 
          led_start_time = 0;
          if (state != LAST_STATE) execute = true;
          break;

        // --- ANIMATED STATES ---
        
        case STATE_ERROR:
        case STATE_NOKETTLE:
          // Fast pulse
          if (now - led_start_time > error_blink_period) {
             pulse_up = !pulse_up;
             led_start_time = now;
             call.set_brightness(pulse_up ? 1.0 : 0.0);
             call.set_transition_length(0);
             execute = true;
          }
          break;

        case STATE_WARMING:
          // Breathing pulse
          if (now - led_start_time > 1000) {
             pulse_up = !pulse_up;
             led_start_time = now;
             call.set_brightness(pulse_up ? 1.0 : 0.2);
             call.set_transition_length(1000);
             execute = true;
          }
          break;
          
        default:
           return;
      }
      if (execute) call.perform();
    }

    // ----------------------------------------------------------------
    // UI & STATE SYNCHRONIZATION
    // ----------------------------------------------------------------
    
    // Helper to silence text sensor spam
    void publish_text_quietly(const char* msg) {
        if (id(fault_status).state != msg) {
            id(fault_status).publish_state(msg);
        }
    }
    
    void sync_ui(int state){

        switch (state) {
          case STATE_OFF:
            set_heater(false);
            if (id(kettle_active).state) id(kettle_active).turn_off();
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            publish_text_quietly("Idle");
            break;

          case STATE_BOILING:
            if (!id(kettle_active).state) id(kettle_active).turn_on();
            if (!id(boiling_status).state) id(boiling_status).publish_state(true);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            publish_text_quietly("Boiling");
            break;

          case STATE_WARMING:
            if (!id(kettle_active).state) id(kettle_active).turn_on();
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (!id(keeping_warm_status).state) id(keeping_warm_status).publish_state(true);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            publish_text_quietly("Keeping Warm");
            break;

          case STATE_DONE:
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            publish_text_quietly("Target Reached");
            break;

          case STATE_NOKETTLE:
            set_heater(false);
            if (id(kettle_active).state) id(kettle_active).turn_off();
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (id(kettle_present).state) id(kettle_present).publish_state(false);
            publish_text_quietly("Kettle Missing");
            break;

          case STATE_ERROR:
            set_heater(false);
            if (id(kettle_active).state) id(kettle_active).turn_off();
            if (id(boiling_status).state) id(boiling_status).publish_state(false);
            if (id(keeping_warm_status).state) id(keeping_warm_status).publish_state(false);
            if (!id(kettle_present).state) id(kettle_present).publish_state(true);
            publish_text_quietly("Error");
            break;
            
          default:
            set_heater(false);
            if (id(kettle_active).state) id(kettle_active).turn_off();
            publish_text_quietly("Internal Error");
            break;
        }    
    }

    void shutdown(const char* msg) {
      set_heater(false);
      if (id(kettle_active).state) id(kettle_active).turn_off();
      id(fault_status).publish_state(msg);
    }
    
  public:
    void loop(int loop_duration) {
      // 1. GATHER INPUTS
      bool active = id(kettle_active).state;
      float current = id(water_temp).state;
      float target = id(target_temp).state;
      float keep_warm = id(keep_warm_mins).state;
      
      if (isnan(current)) {
         set_heater(false);
         return;
      }
      
      if (last_temp == 0.0) last_temp = current;
      
      // ----------------------------------------------------------
      // Calculate Real-time Rate (dT/dt)
      // ----------------------------------------------------------
      unsigned long now = millis();
      unsigned long dt_ms = now - last_calc_time;

      // Only calculate if enough time has passed (prevents divide by zero)
      if (dt_ms >= 500) {
         float dt_sec = dt_ms / 1000.0;
         float raw_diff = current - last_temp;
         float instant_rate = raw_diff / dt_sec;

         // SMOOTHING (Low Pass Filter)
         // alpha = 0.2 means: "Trust the new value 20%, keep 80% of old average"
         // This kills sensor jitter.
         float alpha = 0.2; 
         current_rate = (alpha * instant_rate) + ((1.0 - alpha) * current_rate);

         // Update History
         last_temp = current;
         last_calc_time = now;
      }

      // 2. DETERMINE STATE
      int state = LAST_STATE; 

      if (current < MIN_TEMP) {
        has_kettle = false; 
        state = STATE_NOKETTLE;
      } else {
        has_kettle = true;
      }
      
      if (active && current_rate > MAX_RATE) {
        state = STATE_ERROR;
        shutdown("Error: Dry Boil");
      }
      else if (current > MAX_TEMP) {
        state = STATE_ERROR;
        shutdown("Error: Overheat");
      }

      // 3. LOGIC TREE
      if (!active && state != STATE_ERROR) {
        set_heater(false);
        hold_start_time = 0;
        if (state != STATE_NOKETTLE) state = STATE_OFF;
      }
      else if (state != STATE_ERROR) {
        // Calculate where we will land if we cut power NOW.
        // Only consider positive rates (if cooling, momentum is zero).
        float momentum = (current_rate > 0) ? (current_rate * THERMAL_LAG) : 0.0;
        float predicted_peak = current + momentum;
        float undershoot = HYST;
        
        // --- HEATING PHASE ---
        if (hold_start_time == 0) { // Boiling
          state = STATE_BOILING;

          // Mark as DONE if we are practically there
          if (current >= target - 0.5) {
            hold_start_time = millis();
            state = STATE_DONE;
            set_heater(false);
          } else undershoot = 0;
        }
        else { // Warming
          unsigned long elapsed = millis() - hold_start_time;
          unsigned long limit = keep_warm * 60 * 1000;
          if (limit == 0 || elapsed > limit) {
             state = STATE_OFF;
             active = false;
          } else{
            state = STATE_WARMING;
          }
        }
        // --- HEATER CONTROL ---
        if (has_kettle) { // Heating logic
           // BRAKING: Cut power if momentum will carry us to target
           if (predicted_peak >= target) {
               set_heater(false);
           } 
           // ACCELERATING: Only turn on if below hysteresis threshold
           // If we are in the "Dead Zone" (e.g. 99C), we do nothing, 
           else if (current < (target - undershoot) && active) {
               set_heater(true);
           }
        } 
        else {
           state = STATE_NOKETTLE;
        }
      }
      // 4. OUTPUTS
      sync_ui(state);
      set_led(state, loop_duration);
      LAST_STATE = state;
    }
};

KettleLogic kettle_logic;