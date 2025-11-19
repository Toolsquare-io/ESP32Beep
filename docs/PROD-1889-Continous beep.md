# PROD-1889: Continuous Beep Issue Analysis

## Overview

The ESP32Beep library occasionally fails to turn off the speaker amplifier, resulting in a continuous beep. This document analyzes the root causes and provides recommendations for fixes.

## Library Architecture

The ESP32Beep library uses:
- ESP32 PWM hardware to generate square waves at configurable frequencies
- A GPIO pin to control an amplifier enable signal (e.g., NCS2211)
- Timing based on `millis()` for pattern execution
- **Cooperative multitasking** - requires periodic calls to `run()` method

## Identified Root Causes

### Root Cause #1: Buffer Overflow in Pattern Shifting (HIGH SEVERITY)

**Location:** [src/ESP32Beep.cpp:113-118](src/ESP32Beep.cpp#L113-L118)

**The Bug:**
```cpp
void Beep::shiftPattern() {
    for (uint8_t i = 0; i < patternBufferLength; i++)        // patternBufferLength = 32
    {
        patternBuffer[i] = patternBuffer[i + 1];
    }
}
```

**Problem:**
- When `i = 31` (last iteration), the code reads `patternBuffer[32]`
- The buffer is `char patternBuffer[33]` (indices 0-32)
- At iteration 31, reading index 32 (the null terminator) and trying to access index 33 reads **undefined memory**
- This shifts garbage data into the buffer

**Impact:**
- Garbage characters replace the null terminator
- Pattern execution never reaches the `0x00` case in `patternCharToTone()`
- The `default` case (line 155-157) keeps the amplifier ON
- Mode remains `PatternExecuting` indefinitely
- **Result: Continuous beep**

**Why This Causes Intermittent Issues:**
- Depends on what garbage data happens to be in memory
- If garbage data contains non-null characters, beep continues
- Timing-dependent based on memory state

---

### Root Cause #2: Incomplete Pattern Buffer Initialization (MEDIUM SEVERITY)

**Location:** [src/ESP32Beep.cpp:52-55](src/ESP32Beep.cpp#L52-L55)

**The Bug:**
```cpp
void Beep::output(const char* pattern) {
    mode = beepModes::PatternExecuting;
    strncpy(patternBuffer, pattern, 16);        // only copies 16 chars!
    patternBuffer[16] = 0x00;                   // terminates at position 16
    // ... rest of buffer (17-32) contains old data
}
```

**Problem:**
- Buffer size is 32 characters (`patternBufferLength = 32`)
- Only first 16 characters are copied and terminated
- Positions 17-32 contain **leftover data from previous patterns**

**Impact:**
- When `shiftPattern()` shifts beyond position 16, it encounters old garbage data
- Old data may not contain proper null terminators
- Pattern execution continues beyond intended duration
- **Result: Extended or continuous beep**

**Interaction with Bug #1:**
- This bug makes Bug #1 worse by ensuring there's garbage data in the buffer
- After 16 shifts, the pattern starts processing old/undefined data

---

### Root Cause #3: Missed `run()` Calls (CRITICAL SEVERITY)

**Location:** Design flaw in the cooperative multitasking model

**The Problem:**

The library has a critical **asymmetry** in how it controls the amplifier:

| Operation | Execution Model | Reliability |
|-----------|----------------|-------------|
| Turn amplifier **ON** | Immediate/Synchronous | Always works |
| Turn amplifier **OFF** | Deferred/Requires `run()` | Depends on firmware |

**Code Flow:**

1. **Turning ON** ([src/ESP32Beep.cpp:45-50](src/ESP32Beep.cpp#L45-L50)):
   ```cpp
   void Beep::output(uint32_t theDuration) {
       mode     = beepModes::DurationOn;
       duration = theDuration;
       setStartTime();
       setAmplifierOnOff(true);  // ← IMMEDIATE
   }
   ```

2. **Turning OFF** ([src/ESP32Beep.cpp:78-82](src/ESP32Beep.cpp#L78-L82)):
   ```cpp
   case Beep::beepModes::DurationOn:
       if (hasExpired()) {
           setAmplifierOnOff(false);  // ← ONLY if run() is called!
           mode = beepModes::Idle;
       }
       break;
   ```

**Failure Scenarios:**

If `run()` stops being called after the amplifier is enabled, it will **never** be disabled.

Common ESP32 scenarios that can cause this:

| Scenario | Description | Likelihood |
|----------|-------------|------------|
| **Blocking operations** | `delay()`, `while()` loops, busy-waiting | HIGH |
| **WiFi operations** | Connection attempts, reconnection, long transmissions | HIGH |
| **I2C/SPI hangs** | Sensor communication failures, bus lockup | MEDIUM |
| **Exception/Crash** | Null pointer, stack overflow, heap corruption | MEDIUM |
| **Watchdog reset** | Task takes too long, system resets mid-beep | MEDIUM |
| **Deep sleep entry** | System enters sleep before timer expires | LOW |
| **RTOS scheduling** | Task preemption, priority inversion | LOW |

**Impact:**
- Timer expires but OFF code never executes
- Amplifier GPIO stays HIGH
- **Result: Continuous beep until power cycle**

**Why This Is Most Likely:**
- Explains **intermittent** nature (depends on firmware timing)
- Common in real-world ESP32 applications with WiFi/sensors
- No hardware protection against firmware failures

---

## Comparison of Root Causes

| Root Cause | Severity | Likelihood | Detectability | Fix Complexity |
|------------|----------|------------|---------------|----------------|
| Buffer Overflow | HIGH | MEDIUM | Hard (memory-dependent) | Easy |
| Incomplete Initialization | MEDIUM | MEDIUM | Hard (depends on usage) | Easy |
| Missed `run()` Calls | **CRITICAL** | **HIGH** | Easy (add logging) | Medium |

---

## Recommended Fixes

### Fix #1: Correct Buffer Overflow

**Change `shiftPattern()` to:**
```cpp
void Beep::shiftPattern() {
    for (uint8_t i = 0; i < patternBufferLength - 1; i++)  // Stop at 31, not 32
    {
        patternBuffer[i] = patternBuffer[i + 1];
    }
    patternBuffer[patternBufferLength - 1] = 0x00;  // Ensure null terminator
}
```

### Fix #2: Properly Initialize Buffer

**Change `output(const char* pattern)` to:**
```cpp
void Beep::output(const char* pattern) {
    mode = beepModes::PatternExecuting;
    memset(patternBuffer, 0, patternBufferLength + 1);  // Clear entire buffer
    strncpy(patternBuffer, pattern, patternBufferLength);  // Copy up to 32 chars
    patternBuffer[patternBufferLength] = 0x00;  // Ensure termination

    duration = defaultDuration;
    patternCharToTone();
    setStartTime();
}
```

### Fix #3: Add Hardware Safety (CRITICAL)

**Option A: Watchdog in `run()` method**

Add a maximum duration check even if timer tracking fails:

```cpp
void Beep::run() {
    // Safety: Force off after maximum duration regardless of mode
    static const uint32_t MAX_BEEP_DURATION = 30000;  // 30 seconds max

    if (mode != beepModes::Idle) {
        unsigned long currentTime = millis();
        if (currentTime - startTime > MAX_BEEP_DURATION) {
            setAmplifierOnOff(false);
            mode = beepModes::Idle;
            return;  // Safety shutdown
        }
    }

    // ... rest of run() logic
}
```

**Option B: Use ESP32 Hardware Timer (RECOMMENDED)**

Replace cooperative multitasking with hardware timer interrupt:
- Set up `esp_timer` when beep starts
- Timer interrupt automatically disables amplifier
- Works even if main loop hangs
- Requires moderate refactoring

**Option C: Amplifier Auto-Shutdown Circuit**

- Add hardware RC circuit or timeout IC to amplifier enable
- Requires periodic refresh signal from ESP32
- Provides ultimate protection against firmware failures

---

## Testing Recommendations

### Test #1: Buffer Overflow Test
```cpp
// Test long patterns
beep.output("4 4 8 8 4 4 8 8 4 4 8 8 4 4 8 8 4 4 8 8");  // >32 chars
// Wait and verify it stops
```

### Test #2: Missed `run()` Test
```cpp
beep.output(440, 1000);
delay(500);
// Deliberately skip run() calls for 2 seconds
delay(2000);
// Check if beep is still on (BUG) or off (FIXED)
```

### Test #3: Firmware Blocking Test
```cpp
beep.output(440, 1000);
// Simulate WiFi connection
WiFi.begin("ssid", "password");
while (WiFi.status() != WL_CONNECTED) {
    // Note: NOT calling beep.run() here
    delay(100);
}
// Check if beep stopped or is continuous
```

---

## Monitoring Recommendations

Add diagnostic logging to firmware:

```cpp
void loop() {
    static unsigned long lastRunCall = 0;
    unsigned long now = millis();

    if (now - lastRunCall > 100) {  // If >100ms since last run()
        Serial.printf("WARNING: run() not called for %lu ms\n", now - lastRunCall);
    }

    theBuzzer.run();
    lastRunCall = now;
}
```

---

## Implementation Status

### ✅ IMPLEMENTED: Hybrid Hardware Timer Safety Solution

**Date:** 2025-11-19
**Branch:** main
**Repository:** <https://github.com/Toolsquare-io/ESP32Beep>

All three root causes have been addressed with the following implementation:

#### 1. Buffer Overflow Fix (Root Cause #1) - COMPLETE

**Files Modified:** [src/ESP32Beep.cpp:138-145](src/ESP32Beep.cpp#L138-L145)

```cpp
void Beep::shiftPattern() {
    // Shift all characters one position forward, avoiding buffer overflow
    for (uint8_t i = 0; i < patternBufferLength - 1; i++) {
        patternBuffer[i] = patternBuffer[i + 1];
    }
    // Ensure buffer remains null-terminated
    patternBuffer[patternBufferLength - 1] = 0x00;
}
```

**Result:** Prevents reading beyond buffer bounds and ensures null termination is maintained.

#### 2. Buffer Initialization Fix (Root Cause #2) - COMPLETE

**Files Modified:** [src/ESP32Beep.cpp:71-84](src/ESP32Beep.cpp#L71-L84)

```cpp
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
    // Start safety timer for patterns
    startSafetyTimer(patternBufferLength * defaultDuration * safetyTimeoutMultiplier);
}
```

**Result:** Entire buffer is cleared before use, preventing garbage data from previous patterns.

#### 3. Hardware Safety Timer (Root Cause #3) - COMPLETE

**Implementation:** Hybrid approach combining cooperative multitasking with hardware safety

**Files Modified:**

- [src/ESP32Beep.h](src/ESP32Beep.h) - Added timer declarations and includes
- [src/ESP32Beep.cpp](src/ESP32Beep.cpp) - Added timer implementation

**Key Components:**

**a) Timer Initialization** ([src/ESP32Beep.cpp:33-42](src/ESP32Beep.cpp#L33-L42)):
```cpp
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
```

**b) Timer Started on Beep** ([src/ESP32Beep.cpp:63-68](src/ESP32Beep.cpp#L63-L68)):
```cpp
void Beep::output(uint32_t theDuration) {
    mode     = beepModes::DurationOn;
    duration = theDuration;
    setStartTime();
    setAmplifierOnOff(true);
    startSafetyTimer(theDuration * safetyTimeoutMultiplier);  // Safety timer at 2x duration
}
```

**c) Timer Cancelled on Normal Completion** ([src/ESP32Beep.cpp:97-102](src/ESP32Beep.cpp#L97-L102)):
```cpp
case Beep::beepModes::DurationOn:
    if (hasExpired()) {
        setAmplifierOnOff(false);
        stopSafetyTimer();  // Cancel safety timer - beep completed normally
        mode = beepModes::Idle;
    }
    break;
```

**d) Safety Callback** ([src/ESP32Beep.cpp:208-218](src/ESP32Beep.cpp#L208-L218)):
```cpp
void IRAM_ATTR Beep::safetyTimerCallback(void* arg) {
    // This callback fires only if run() was not called in time
    // It's a safety mechanism to prevent continuous beep
    Beep* instance = static_cast<Beep*>(arg);
    if (instance != nullptr) {
        instance->setAmplifierOnOff(false);  // Force amplifier OFF
        instance->mode = beepModes::Idle;
    }
}
```

**How It Works:**

1. **Normal Operation:**
   - User calls `output(1000)` → Amplifier ON, safety timer starts at 2000ms
   - `run()` is called periodically
   - After 1000ms, `run()` detects expiry → Amplifier OFF, safety timer cancelled
   - Safety timer never fires ✓

2. **Firmware Blocked (Bug Scenario):**
   - User calls `output(1000)` → Amplifier ON, safety timer starts at 2000ms
   - Firmware gets stuck in WiFi/I2C/blocking operation
   - `run()` stops being called
   - After 2000ms, **safety timer fires automatically** → Amplifier OFF ✓
   - Beep is stopped despite firmware hang ✓

**Advantages:**

- ✅ Maintains backward compatibility - existing code still works
- ✅ No hardware changes required
- ✅ Minimal CPU overhead - hardware timer runs independently
- ✅ Works even if main loop hangs/crashes
- ✅ 2x timeout gives margin for normal operation while catching failures
- ✅ Fixes all three root causes simultaneously

**Hardware Requirements:**

- None - uses ESP32 built-in `esp_timer` peripheral
- No PCB changes needed
- No additional components required

---

## Conclusion

The continuous beep issue has **three distinct root causes**, with the **missed `run()` calls** being the most critical and likely cause in production firmware.

**Recommended Action Plan:**
1. **Immediate:** Implement Fix #3 Option A (watchdog in `run()`)
2. **Short-term:** Apply Fixes #1 and #2 (buffer handling)
3. **Long-term:** Consider Fix #3 Option B (hardware timer) for robust solution

**Priority:** HIGH - Can cause permanent beep requiring power cycle
