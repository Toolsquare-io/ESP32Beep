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
    // Clean up safety timer
    if (safetyTimer != nullptr) {
        esp_timer_stop(safetyTimer);
        esp_timer_delete(safetyTimer);
        safetyTimer = nullptr;
    }
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

    // Create safety timer to prevent continuous beep if run() is not called
    if (safetyTimer == nullptr) {
        esp_timer_create_args_t timerConfig = {
            .callback = &Beep::safetyTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "beep_safety"
        };
        esp_timer_create(&timerConfig, &safetyTimer);
    }
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
    startSafetyTimer(theDuration * safetyTimeoutMultiplier);  // Safety timer at 2x duration
}

void Beep::output(const char* pattern) {
    mode = beepModes::PatternExecuting;
    // Clear entire buffer first to avoid garbage data
    memset(patternBuffer, 0, patternBufferLength + 1);
    // Copy up to patternBufferLength characters
    strncpy(patternBuffer, pattern, patternBufferLength);
    patternBuffer[patternBufferLength] = 0x00;  // Ensure proper termination

    duration = defaultDuration;
    patternCharToTone();
    setStartTime();
    // Start safety timer for patterns (max pattern length * default duration * 2)
    startSafetyTimer(patternBufferLength * defaultDuration * safetyTimeoutMultiplier);
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
                stopSafetyTimer();  // Cancel safety timer - beep completed normally
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
    // Shift all characters one position forward, avoiding buffer overflow
    for (uint8_t i = 0; i < patternBufferLength - 1; i++) {
        patternBuffer[i] = patternBuffer[i + 1];
    }
    // Ensure buffer remains null-terminated
    patternBuffer[patternBufferLength - 1] = 0x00;
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
            stopSafetyTimer();  // Cancel safety timer - pattern completed normally
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

void Beep::startSafetyTimer(uint32_t timeoutMs) {
#ifndef WIN32
    if (safetyTimer != nullptr) {
        // Stop any existing timer first
        esp_timer_stop(safetyTimer);
        // Start timer with timeout in microseconds
        esp_timer_start_once(safetyTimer, static_cast<uint64_t>(timeoutMs) * 1000);
    }
#endif
}

void Beep::stopSafetyTimer() {
#ifndef WIN32
    if (safetyTimer != nullptr) {
        esp_timer_stop(safetyTimer);
    }
#endif
}

void IRAM_ATTR Beep::safetyTimerCallback(void* arg) {
    // This callback fires only if run() was not called in time
    // It's a safety mechanism to prevent continuous beep
    Beep* instance = static_cast<Beep*>(arg);
    if (instance != nullptr) {
        instance->setAmplifierOnOff(false);  // Force amplifier OFF
        instance->mode = beepModes::Idle;
        // Note: In a production system, you might want to log this event
        // to help diagnose firmware issues that prevent run() from being called
    }
}