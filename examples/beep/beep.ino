#include <ESP32Beep.h>

#define audioOutputPin 26U
#define amplifierEnablePin 27U

Beep theBuzzer{audioOutputPin, amplifierEnablePin};

void setup() {
    theBuzzer.initialize();
    theBuzzer.output(440, 1000);        // single 440 Hz, 1000ms beep
}

// the loop function runs over and over again until power down or reset
void loop() {
    theBuzzer.run();        // call run() in loop(), so the buzzer can keep it's timing..
}
