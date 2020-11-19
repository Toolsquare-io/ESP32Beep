// #############################################################################
// ###                                                                       ###
// ### General Purpose Beep library for ESP32                                ###
// ### https://github.com/Strooom/ESP32Beep                                  ###
// ### Author(s) : Pascal Roobrouck - @strooom                               ###
// ### License : https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode ###
// ###                                                                       ###
// #############################################################################

// This class can generate Beeps through the built-in audio hardware of the ESP32
// It uses the built-in PWM channels. Duty cycle is always set to 50%, but we vary the frequency.
// It generates a square wave on the GPIO of your choice
// Furthermore, the on/off behaviour is controlled thru a second GPIO, which will enable/disable the audio amplifier.

#pragma once

#ifndef WIN32
#include <Arduino.h>
#else
#include <inttypes.h>

#include <cstring>
#endif

class Beep {
  public:
    Beep(uint8_t AudioOutput, uint8_t AmplifierEnable);        // Constructor
    ~Beep();                                                   // Destructor
    void initialize();
    void output(float frequency, uint32_t duration);          // send out a beep, frequency in Hz, duration in ms
    void output(float frequency, const char* pattern);        // send out a beep, according to a certain pattern
    void output(bool on);
    void output(uint32_t duration);          // send out a beep, current frequency in Hz, duration in ms
    void output(const char* pattern);        // send out a beep, according to a certain pattern,

    bool isReady() const;        // returns true when current beep pattern is ready..
    void run();                  // background processing for beeps in progress

#if !defined(UnitTesting) && !defined(UNIT_TEST)
  private:        // commented out during unit testing
#endif
    static constexpr double defaultFrequency = 440;
    static constexpr int PWMchannel = 0;
    static constexpr int PWMresolution = 8;
    static constexpr uint32_t defaultDuration = 100;

    enum class beepModes : uint8_t {
        Idle = 0,                   // No beeps running
        BoolOn = 1,                 // Permanently On
        DurationOn = 2,             // On for certain time
        PatternExecuting = 3        // busy executing a pattern
    } mode = beepModes::Idle;

    uint8_t audioOutput;            // GPIO where the beeps are sent out
    uint8_t amplifierEnable;        // GPIO which controls the amplifier
    uint32_t duration;              // nmbr of ms the current beep will last
    unsigned long startTime;        // millis() value at the start of the beep

    static constexpr uint8_t patternBufferLength = 32;        // 32 * 125 ms = 4 seconds for the longest beep..
    char patternBuffer[patternBufferLength + 1];              // +1 for terminating zero

    void setAmplifierOnOff(bool on);
    void setPWMFrequency(float frequency);
    void shiftPattern();
    void setStartTime();
    bool hasExpired();
    void patternCharToTone();
};