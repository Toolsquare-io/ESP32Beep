[![Arduino CI](https://github.com/strooom/ESP32Beep/workflows/Arduino%20Library%20Checks/badge.svg)](https://github.com/marketplace/actions/arduino_ci)

# ESP32Beep
Driver to generate tones &amp; beeps on the ESP32

The library uses the PWM hardware of the ESP32, with a 50% dutycycle to generate a square wave on the selected IO.
This signal then needs to be sent to an amplifier, eg. NCS2211 and by controlling the enable of this amplifier, tone patterns can be generated. Timing for the tone patterns is derived from millis()
