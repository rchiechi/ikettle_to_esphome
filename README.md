I have a Smarter iKettle v3 and I really like the features and aesthetics, but since they stopped paying the bills Electric Imp has soft-bricked the kettles by blocking them from accessing their cloud infrastructure. Below is a description of how I replaced the Electric Imp controller with an ESP32 board. I wired everything by jamming the pins of jumper wires into the existing connectors, so it is completely non-destructive and reversible sans four small solder points.

> Disclaimer: This project involves mains electricity (120V/240V). Modifying heating appliances carries risks of fire or electrocution. This software includes safety watchdogs, but use this code and hardware modification entirely at your own risk.

# Parts Used:

- [Adafruit ESP32-S2 Feather](https://learn.adafruit.com/adafruit-esp32-s2-feather) You can use any ESP32 you like, but this one is high quality and fits neatly in the base.
- Optional: [Adafruit HX711 24-bit ADC](https://learn.adafruit.com/adafruit-hx711-24-bit-adc) if you want to try to get the strain gauge working for the water level (I'm still waiting for mine to arrive so I haven't tested it yet)
- 47 kOhm resistor
- 10 kOhm resistor
- 100 Ohm resistor
- Jumper wires

# Kettle Innards
- Layout: the kettle uses an Electric Imp controller to read a strain gauge, NTC thermsistor and toggle a relay switch for heating. The [circuit diagram](https://fccid.io/2AKC5-SMKET01/Schematics/Circuit-Diagram-3267208.pdf) shows a 10 kOhm resistor across the NTC, but I measured mine to be 50 kOhm.
- Main board: this is the longer of the two PCBs. It houses the Electric Imp chip, strain sensor amplifier, WiFi, physical button and LEDs.
- Daughter board: this is the smaller of the two PCBs. It houses the AC/DC transformer for 5V power and the relay to connect mains power to the kettle base. The two boards are connected by three white wires. The outer wires are 5V DC power and the Heater Relay, the middle is GND. If you unscrew the daughter board and flip it over you can see the three pins from the wires, which I've labeled in the picture below. Make sure you correctly map them to the connector pins on the top of the board.
![Bottom of Daughter Board|690x285](images/IMG_0435_stripped.jpg)
- Kettle base connector. This the piece of plastic in the middle with concentric rings and some thick wires coming out of it.
- NTC [thermsistor](https://en.wikipedia.org/wiki/Thermistor) connector. The kettle houses a 50 kOhm thermsistor that makes contact in the second and third rings from the innermost ring. Look for a thin black and thin red wire that terminate at a connector on the main board.
- Physical button. This is just a momentary switch soldered on to the main board. There are four posts. The pair closest to the front edge (nearest the button when assembled) closes when the button is depressed, this is where I soldered wires for the button switch.
- LED ring: the LED ring light sits just above the button on two posts. If you look at the clear plastic part, you will see the post (smaller electrode) and anvil (larger). The post is the anode (+) and it needs a 100 Ohm resistor between it and the GPIO pin.
- The strain gauge is attached to the rubber foot of the base. I de-soldered the three wires from the amplifier on the main board for future connection to an external amplifier, so you just see the empty pads on the main board. I hope to get that working once my HX711 arrives.
  ![Strain gauge||666x500](images/IMG_0433.jpg)

# Connections

- Disconnect the plug from the main board, leaving the other end plugged into the daughter. The free end of the connector has three female pins: insert a jumper wire into the center pin and connect it the common ground and the GND terminal on the Feather. I just soldered a bunch of pins together and tied all the ground wires together.
- Figure out which pin is the Heater Relay and which is the 5V. **Warning** It is super important not to mix them up because you can fry the Feather by putting 5V on the GPIO pin.
- Connect the 5V pin to the USB (or VBUS) pin on the Feather (this is called different things on different boards, but it is the input voltage that powers the whole board). **Do not** connect it to the 3V pin.
- Connect the Heater Relay to GPIO12. **Note:** the relay Heater Relay pin takes 3.3V even though the relay is a 5V switch; the daughter board uses a MOSFET to pull the relay to 5V.
- To be clear: the GND, 5V and GPIO12 pins terminate on the daughter board, via the white wire; we are effectively replacing the entire main board with the Feather.
- Disconnect the NTC connector from the main board. (In the picture above, you see the male pins on the main board after the connector is removed.) The free end of the connector has two female pins: Connect the red wire to GPIO A5 and the black wire to GND. (It actually doesn't matter which is which.) It is essential that you use a pin on [ADC1](https://cdn-learn.adafruit.com/assets/assets/000/110/677/original/adafruit_products_Adafruit_Feather_ESP32-S2_Pinout.png?1649709383) because ADC2 is used by WiFi.
- Connect the other end of the 100 Ohm resistor from the LED anode to GPIO 10.
- Connect the LED cathode (the one connected to the anvil, the larger electrode) to GND
- Connect one of the physical button wires to GPIO5 (doesn't matter which one) and the other to GND
 - On the back of the Feather, solder a 10 kOhm resistor from 3V -> GPIO5
 - On the back of the Feather, solder a 47 kOhm resistor from 3V -> GPIOA5
   ![Feather Wiring|529x361](images/iKettle.png)

# ESPHome Yaml

- If you use an ESP32-S2 Feather wired the way I have it pictured, you only have to update the encryption key and wifi credentials
- The full esphome yaml is [here](eyeKettle.yaml)
- If you use a different board, you can define the pins in the "Configuration Variables" section
```yaml

# -----------------------------------------------------------------
# Configuration variables
# -----------------------------------------------------------------
substitutions:
  temperature_pin: GPIO8  # labeled 'A5' must be ADC1 to avoid wifi conflict
  temperature_resistor: 47kOhm # Ideally 47 - 100 kOhm
  led_pin: GPIO10
  button_pin: GPIO5 # 10 kOhm resistor to 3.3V
  heating_pin: GPIO12
  default_target_temp: 85 # The default target temp on startup

```

# Example HA usage

- I wrote this to support the things that I use, which is mainly heating to 85 °C or 100 °C depending on if I'm making tea or the kids are making ramen. You can use the exposed HA controls to script it however you like.
- The button presses are captured as actions like:
```yaml
alias: Kettle button
description: Handle physical button presses
triggers:
  - event_type: esphome.kettle_button_event
    id: single_press
    event_data:
      click_type: single
    trigger: event
  - event_type: esphome.kettle_button_event
    id: double_press
    event_data:
      click_type: double
    trigger: event
conditions:
  - condition: time
    after: "05:00:00"
    before: "20:00:00"
actions:
  - variables:
      target_setpoint: |
        {% if trigger.id == 'single_press' %}
          100
        {% else %}
          85
        {% endif %}
  - choose:
      - conditions:
          - condition: state
            entity_id: binary_sensor.eyekettle_boiling
            state:
              - "off"
        sequence:
          - action: number.set_value
            target:
              entity_id: number.target_temperature
            data:
              value: "{{ target_setpoint }}"
          - action: switch.turn_on
            target:
              entity_id:
                - switch.eyekettle_start_heating
            data: {}
          - action: tts.speak
            metadata: {}
            target:
              entity_id: tts.piper
            data:
              cache: true
              media_player_entity_id: media_player.dinguses
              message: Kettle heating to {{ target_setpoint }} degrees Celsius
    default:
      - action: switch.turn_off
        target:
          entity_id:
            - switch.eyekettle_start_heating
        data: {}
      - action: tts.speak
        metadata: {}
        target:
          entity_id: tts.piper
        data:
          cache: true
          media_player_entity_id: media_player.dinguses
          message: Turned off the kettle
mode: single

```
- The rest of the switches and sensors are exposed in HA


# Strain Gauge

If you are *not* using the strain gauge you must comment it out of the YAML config. It is clearly indicated. You must also comment out the `hx711: INFO` line in `logger:`

If you want to enable the water level sensor, you need a strain gauge amplifier. I used the [Adafruit HX711 24-bit ADC](https://learn.adafruit.com/adafruit-hx711-24-bit-adc/pinouts). The strain gauge in the kettle is a half-cell, meaning you need to construct a reference cell with two 1 kOhm resistors like this:

```plaintext
HX711 Board
      +-----------+
      |           |
      |        E+ |<---------+------------ Connect Sensor BLACK Wire
      |           |          |
      |           |          |
      |           |     [ Fixed R1 ]  <--- (Your first dummy resistor)
      |           |          |
      |           |          |
      |        A- |<---------+------------ Connect Junction of R1 & R2
      |           |          |
      |           |          |
      |           |     [ Fixed R2 ]  <--- (Your second dummy resistor)
      |           |          |
      |           |          |
      |        E- |<---------+------------ Connect Sensor WHITE Wire
      |           |
      |           |
      |        A+ |<---------------------- Connect Sensor RED Wire
      |           |
      +-----------+
```

There is enough space in the base that you can tuck all the wires and the two PCBs of the feather and HX711 neatly inside.
![Bottom of Daughter Board|690x285](images/ikettle_strain_.jpg)

In order to use the strain gauge, it has to be calibrated. There are two places in the YAML you need to find to do this.

1. Find the logger entry. Comment out the line to enable the DEBUG output in the console. Compile and install. Every second it will spam the console with raw readings of a siged 24 bit integer. You need to write down the values when: the kettle is empty; with 1L of water in it; and when full. Give it a bit to settle before writing down the numbers, as the sloshing of water causes them to drift quite a bit. 

```yaml
logger:
  level: DEBUG
  logs:
    hx711: INFO # Comment for calibration, only uncomment if using HX711
```

2. Find this block in the YAML and populate it with your numbers for reference 

```plaintext
    ##################################################
    # These are my values: yours will be different!  #
    # No Kettle: -522425 / -520781                   #
    # Empty Kettle: -434452 / -436118                #
    # 1 L of water in Kettle: -356995 / -358771      #
    # Full Kettle: -295012 /  -296228                #
    ##################################################
```

3. Find this block in the YAML and enter your numbers:


```yaml
      # CALIBRATION (Based on your averages)
      # Maps raw weight directly to Percentage (0.0 - 100.0)
      - calibrate_linear:
          - -435300 -> 0.0 # Point 1: Empty
          - -357900 -> 1.0 # Point 2: 1.0 Liter
          - -295600 -> 1.8 # Point 3: 1.8 Liters (Full)
```

4. Un-comment the `hx711: INFO` line to supress the raw output
5. Recompile and install.

# Home Assistant

![Home Assistant Integration|690x285](images/HA_integration.png)

The YAML exposes the following:

- Controls:
  - switch for the heating element
  - keep warm timer (0 to disable)
  - target temperature
- Sensors:
  - boiling, binary switch that is on when target temp is reached
  - keeping warm, binary switch that is on when keeping warm is active
  - kettle present, binary switch that is on when kettle is on the base
  - water level, percentage sensor of approximate water level (if using hx711)
  - water temperature, shows current water temperature if kettle is on base
- Actions:
 - esphome.kettle_button_event is fired when the button is pressed