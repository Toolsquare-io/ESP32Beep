// #############################################################################
// ###                                                                       ###
// ### Generating beeps with PWM HW on the ESP32                             ###
// ### https://github.com/Strooom/ESP32Beep                                  ###
// ### Author(s) : Pascal Roobrouck - @strooom                               ###
// ### License : https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode ###
// ###                                                                       ###
// #############################################################################

#include "ESP32Beep.h"

Beep::Beep(uint8_t theAmplifierEnable) : mode{beepModes::Idle}, amplifierEnable{theAmplifierEnable}, duration{0}, startTime{0} {
    patternBuffer[0] = 0x00;        // terminate an empty string in the patternBuffer
}

Beep::~Beep() {
#ifndef WIN32
    ledcDetachPin(audioOutput);             // TODO : check if there are other things to clean up
    pinMode(amplifierEnable, INPUT);        // reset this pin as an input
#endif
}

void Beep::initialize(uint8_t theAudioOutput) {
    audioOutput = theAudioOutput;
#ifndef WIN32
    pinMode(amplifierEnable, OUTPUT);        // set this pin as an output towards the amplifier
#endif
    setAmplifierOnOff(false);
#ifndef WIN32
    ledcSetup(PWMchannel, defaultFrequency, PWMresolution);        // Configure a PWM driver/channel
    ledcAttachPin(audioOutput, PWMchannel);                        // Attach its output to a pin
#endif
    setPWMFrequency(defaultFrequency);        // Enable the PWM output with a default frequency
    mode = beepModes::Idle;
}

void Beep::output(bool onOff) {
    setAmplifierOnOff(onOff);
    if (onOff) {
        mode = beepModes::BoolOn;
    } else {
        mode = beepModes::Idle;
    }
}

void Beep::output(uint32_t theDuration) {
    mode     = beepModes::DurationOn;
    duration = theDuration;
    setStartTime();
    setAmplifierOnOff(true);
}

void Beep::output(const char* pattern) {
    mode = beepModes::PatternExecuting;
    strncpy(patternBuffer, pattern, 16);        // copy new pattern to beep's internal buffer
    patternBuffer[16] = 0x00;                   // ensure proper termination

    duration = defaultDuration;
    patternCharToTone();
    setStartTime();
}

bool Beep::isReady() const {
    return (mode == beepModes::Idle);
}

void Beep::output(float frequency, uint32_t theDuration) {
    setPWMFrequency(frequency);
    output(theDuration);
}

void Beep::output(float frequency, const char* pattern) {
    setPWMFrequency(frequency);
    output(pattern);
}

void Beep::run() {
    switch (mode) {
        case Beep::beepModes::DurationOn:
            if (hasExpired()) {
                setAmplifierOnOff(false);
                mode = beepModes::Idle;
            }
            break;
        case Beep::beepModes::PatternExecuting: {
            if (hasExpired()) {
                shiftPattern();
                patternCharToTone();
                setStartTime();
            }
        } break;
        default:
            // do nothing
            break;
    }
}

void Beep::setAmplifierOnOff(bool on) {
#ifndef WIN32
    if (on) {
        digitalWrite(amplifierEnable, HIGH);
    } else {
        digitalWrite(amplifierEnable, LOW);
    }
#endif
}

void Beep::setPWMFrequency(float frequency) {
#ifndef WIN32
    ledcWriteTone(PWMchannel, static_cast<double>(frequency));        // set frequency correct
#endif
}

void Beep::shiftPattern() {
    for (uint8_t i = 0; i < patternBufferLength; i++)        // shift all characters one position forward
    {
        patternBuffer[i] = patternBuffer[i + 1];
    }
}

void Beep::setStartTime() {
#ifndef WIN32
    startTime = millis();        // remember start time
#endif
}

bool Beep::hasExpired() {
    unsigned long currentTime = 0;
#ifndef WIN32
    currentTime = millis();
#endif
    return (currentTime - startTime >= duration);
}

void Beep::patternCharToTone() {
    switch (patternBuffer[0]) {
        case 0x00:
            setAmplifierOnOff(false);
            mode = beepModes::Idle;
            break;

        case ' ':
            setAmplifierOnOff(false);
            break;

        case '4':
            setPWMFrequency(440.0);
            setAmplifierOnOff(true);
            break;

        case '8':
            setPWMFrequency(880.0);
            setAmplifierOnOff(true);
            break;

        default:
            setAmplifierOnOff(true);
            break;
    }
}