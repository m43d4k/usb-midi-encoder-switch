// SPDX-License-Identifier: MIT
// Copyright (c) 2025 m43d4k

#include <MIDIUSB.h>
#include <stdint.h>

// =========================
// User-tunable area (summary)
// =========================
namespace CFG {
  // ----- Rest (phase) policy -----
  // If there is no movement or the feel is off, toggle Hard00or11 <-> Hard01or10.
  enum class RestPolicy : uint8_t { Hard00or11, Hard01or10 };
  constexpr RestPolicy REST_POLICY = RestPolicy::Hard00or11;

  // ----- Wiring & Polarity -----
  constexpr bool USE_INTERNAL_PULLUP = false; // Encoder pins only; switches are INPUT_PULLUP. Set false if using external pull-ups.
  constexpr bool ENCODER_INVERT      = true;  // Flip if the direction feels reversed

  // ----- MIDI (global) -----
  constexpr uint8_t midiChannel = 1;          // 1..16 (fixed at compile time)
  constexpr byte    encoderCC   = 10;         // Encoder CC (0..119 recommended)

  // ----- Switches & MIDI mapping -----
  struct SwitchCfg { int pin; byte cc; };
  constexpr SwitchCfg SWITCHES[] = {
    {18, 21}, {19, 22}, {20, 23}
  };

  // ----- LED (PWM required on Pro Micro) -----
  // PWM-capable pins on Pro Micro: 3/5/6/9/10
  constexpr int    ledPin      = 9;   // PWM (3/5/6/9/10)
  // Index in the SWITCHES array to follow (0-based). Use -1 for no follow.
  constexpr int8_t LED_FOLLOWS_SWITCH_INDEX = 0;
  constexpr byte   LED_BRIGHTNESS_ON  = 32;   // Base ON brightness
  constexpr byte   LED_BRIGHTNESS_OFF = 0;    // Base OFF brightness
  // Transmit activity pulse (temporarily overrides brightness on MIDI send)
  constexpr bool          LED_ACTIVITY_PULSE    = false; // Set true to enable
  constexpr byte          LED_PULSE_BRIGHTNESS  = 64;    // Brightness during pulse
  constexpr unsigned long LED_PULSE_MS          = 60;    // Pulse duration (ms)

  // ----- Slow-mode feel -----
  constexpr unsigned long SLOW_ENTER_MS = 30;
  constexpr unsigned long SLOW_EXIT_MS  = 18;
  constexpr unsigned long IMMEDIATE_OUTPUT_THRESHOLD_MS = 34;

  // ----- Acceleration (detent-interval based) -----
  constexpr bool          USE_DETENT_ACCELERATION = true;
  constexpr unsigned long DETENT_THRESHOLDS_MS[] = {30, 38, 48, 60, 75, 95, 120, 170};
  constexpr byte          DETENT_STEPS[]         = {13, 10, 8, 7, 5, 3, 2, 1};

  // (Reference: switch to edge-interval based if preferred)
  constexpr unsigned long EDGE_ACC_THRESHOLDS[]  = {6, 12, 18, 24, 32, 40, 56, 80};
  constexpr byte          EDGE_ACC_STEPS[]       = {11, 9, 7, 5, 4, 3, 2, 1};

  // ----- Step hysteresis -----
  constexpr unsigned long STEP_HYST_UP_MS   = 3;
  constexpr unsigned long STEP_HYST_DOWN_MS = 6;

  // ----- Gating (return-to-rest) -----
  constexpr bool FULL_CYCLE_GATING = true;

  // ----- Output pacing -----
  constexpr unsigned long OUTPUT_TICK_MS = 2;
  constexpr byte          MAX_PER_TICK   = 3;

  // ----- Switches -----
  constexpr unsigned long BUTTON_DEBOUNCE_MS = 15;

  // ----- Safety -----
  constexpr long          OUTACCUM_SOFT_LIMIT = 3000;

  // ----- ADVANCED (normally do not touch) -----
  constexpr unsigned long FILTER_INTERVAL_US = 400; // Integrating debounce period
  constexpr uint8_t       INTEGR_MAX         = 4;    // Integration width (effective ~INTEGR_MAX*0.4ms)
  constexpr unsigned long REST_DEBOUNCE_MS   = 2;
  constexpr unsigned long ENCODER_DIRECTION_CHANGE_SUPPRESS_MS = 24;
  constexpr unsigned long ENCODER_STABLE_DELAY_MS              = 1;

  // ----- Pins (Pro Micro) -----
  constexpr int encoderPinA = 4;   // e.g., D4
  constexpr int encoderPinB = 5;   // e.g., D5
}

// =========================
/* Compile-time sanity checks */
// =========================
template <typename T, size_t N> constexpr size_t array_size_ct(const T (&)[N]) { return N; }
constexpr size_t NUM_SWITCHES = array_size_ct(CFG::SWITCHES);

// MIDI range
static_assert(CFG::midiChannel >= 1 && CFG::midiChannel <= 16, "midiChannel must be 1..16");
static_assert(CFG::encoderCC <= 119, "encoderCC should be 0..119 (120-127 are Channel Mode)");

// Switch CC 0..119 / pin >= 0
template <size_t I, size_t N>
struct CheckSwitchCC { static constexpr bool value = (CFG::SWITCHES[I].cc <= 119) && CheckSwitchCC<I+1,N>::value; };
template <size_t N>
struct CheckSwitchCC<N,N> { static constexpr bool value = true; };
static_assert(CheckSwitchCC<0, NUM_SWITCHES>::value, "Switch CCs should be 0..119");

template <size_t I, size_t N>
struct CheckSwitchPinNonNeg { static constexpr bool value = (CFG::SWITCHES[I].pin >= 0) && CheckSwitchPinNonNeg<I+1,N>::value; };
template <size_t N>
struct CheckSwitchPinNonNeg<N,N> { static constexpr bool value = true; };
static_assert(CheckSwitchPinNonNeg<0, NUM_SWITCHES>::value, "Switch pins must be >= 0");

// LED follow index (-1 disables)
static_assert(CFG::LED_FOLLOWS_SWITCH_INDEX < 0 ||
              (size_t)CFG::LED_FOLLOWS_SWITCH_INDEX < NUM_SWITCHES,
              "LED_FOLLOWS_SWITCH_INDEX out of range");

// Recommended ranges, etc.
static_assert(CFG::INTEGR_MAX >= 2 && CFG::INTEGR_MAX <= 8,      "INTEGR_MAX recommended 2..8");
static_assert(CFG::SLOW_EXIT_MS < CFG::SLOW_ENTER_MS,            "SLOW_EXIT_MS must be < SLOW_ENTER_MS");
static_assert(CFG::STEP_HYST_UP_MS <= CFG::STEP_HYST_DOWN_MS,    "STEP_HYST_UP_MS should be <= STEP_HYST_DOWN_MS");
static_assert(CFG::OUTPUT_TICK_MS >= 1,                          "OUTPUT_TICK_MS must be >= 1");
static_assert(CFG::MAX_PER_TICK >= 1 && CFG::MAX_PER_TICK <= 16, "MAX_PER_TICK should be 1..16");

// Array length consistency
static_assert(array_size_ct(CFG::DETENT_THRESHOLDS_MS) == array_size_ct(CFG::DETENT_STEPS),
              "DETENT_* arrays length mismatch");
static_assert(array_size_ct(CFG::EDGE_ACC_THRESHOLDS) == array_size_ct(CFG::EDGE_ACC_STEPS),
              "EDGE_ACC_* arrays length mismatch");

// ---- Additional patch: array checks (shallower template depth) ----
// Strictly increasing (compare I and I+1; stop when (I+1>=N))
template <size_t I, size_t N, bool Done = (I + 1 >= N)>
struct StrictIncreasing {
  static constexpr bool value =
    (CFG::DETENT_THRESHOLDS_MS[I] < CFG::DETENT_THRESHOLDS_MS[I + 1]) &&
    StrictIncreasing<I + 1, N>::value;
};
template <size_t I, size_t N>
struct StrictIncreasing<I, N, true> { static constexpr bool value = true; };
static_assert(StrictIncreasing<0, array_size_ct(CFG::DETENT_THRESHOLDS_MS)>::value,
              "DETENT_THRESHOLDS_MS must be strictly increasing");

// All elements within 1..127
template <size_t I, size_t N>
struct CheckStepRange {
  static constexpr bool value =
    (CFG::DETENT_STEPS[I] >= 1 && CFG::DETENT_STEPS[I] <= 127) &&
    CheckStepRange<I + 1, N>::value;
};
template <size_t N>
struct CheckStepRange<N, N> { static constexpr bool value = true; };
static_assert(CheckStepRange<0, array_size_ct(CFG::DETENT_STEPS)>::value,
              "DETENT_STEPS must be in 1..127 for all elements");

// Non-increasing (compare I and I+1; stop when (I+1>=N))
template <size_t I, size_t N, bool Done = (I + 1 >= N)>
struct NonIncreasing {
  static constexpr bool value =
    (CFG::DETENT_STEPS[I] >= CFG::DETENT_STEPS[I + 1]) &&
    NonIncreasing<I + 1, N>::value;
};
template <size_t I, size_t N>
struct NonIncreasing<I, N, true> { static constexpr bool value = true; };
static_assert(NonIncreasing<0, array_size_ct(CFG::DETENT_STEPS)>::value,
              "DETENT_STEPS must be non-increasing");

// No duplicate pins (encoder A/B, LED, each switch)
template <size_t I> struct UsedPin;
template <> struct UsedPin<0> { static constexpr int value = CFG::encoderPinA; };
template <> struct UsedPin<1> { static constexpr int value = CFG::encoderPinB; };
template <> struct UsedPin<2> { static constexpr int value = CFG::ledPin; };
template <size_t I> struct UsedPin { static constexpr int value = CFG::SWITCHES[I - 3].pin; };

constexpr size_t TOTAL_PINS_USED = 3 + NUM_SWITCHES;

template <size_t I, size_t J, size_t N>
struct UniqueHelper { static constexpr bool value = (UsedPin<I>::value != UsedPin<J>::value) && UniqueHelper<I, J + 1, N>::value; };
template <size_t I, size_t N>
struct UniqueHelper<I, N, N> { static constexpr bool value = true; };
template <size_t I, size_t N>
struct UniqueRows { static constexpr bool value = UniqueHelper<I, I + 1, N>::value && UniqueRows<I + 1, N>::value; };
template <size_t N>
struct UniqueRows<N, N> { static constexpr bool value = true; };
static_assert(UniqueRows<0, TOTAL_PINS_USED>::value, "Pins must be unique across encoder/LED/switches");

// Guards for Pro Micro (ATmega32U4)
#if defined(__AVR_ATmega32U4__)
constexpr bool isPwmProMicro(uint8_t p) { return p==3 || p==5 || p==6 || p==9 || p==10; }
static_assert(isPwmProMicro(CFG::ledPin), "Pro Micro: ledPin must be a PWM pin (3,5,6,9,10)");
constexpr bool isValidProMicroDigitalPin(int p) {
  return ((p >= 0 && p <= 10) || (p >= 14 && p <= 16) || (p >= 18 && p <= 21));
}
template <size_t I, size_t N>
struct AllPinsAreValid {
  static constexpr bool value = isValidProMicroDigitalPin(UsedPin<I>::value) && AllPinsAreValid<I + 1, N>::value;
};
template <size_t N>
struct AllPinsAreValid<N, N> { static constexpr bool value = true; };
static_assert(AllPinsAreValid<0, TOTAL_PINS_USED>::value,
  "Pro Micro: encoder/LED/switch pins must be within D0-D10, D14-D16, and D18-D21");
#endif

// =========================
// (Below: typically do not edit)
// =========================

template <typename T, size_t N>
constexpr size_t array_size(const T (&)[N]) { return N; }

// ---- USB-MIDI ----
constexpr byte USB_MIDI_CABLE     = 0;
constexpr byte CIN_CONTROL_CHANGE = 0x0B;
constexpr byte MIDI_STATUS_CC     = 0xB0;
static inline byte statusCC() { return MIDI_STATUS_CC | (CFG::midiChannel - 1); }
static inline void sendCC(byte cc, byte value) {
  const byte header = (USB_MIDI_CABLE << 4) | CIN_CONTROL_CHANGE;
  const byte status = statusCC();
  midiEventPacket_t event = { header, status, cc, value };
  MidiUSB.sendMIDI(event);
}

// ---- State ----
uint8_t aIntegr = 0, bIntegr = 0;
uint8_t aStable = 1, bStable = 1;
unsigned long lastFilterUs = 0;

int lastDirection = 0;
unsigned long lastDirectionChangeTime = 0;
unsigned long lastMoveTime            = 0;

unsigned long lastEdgeTimeMs   = 0;
unsigned long lastEdgeInterval = 0;
bool haveEdgeInterval = false;

bool slowMode = true;

int   cycleAccum = 0;
unsigned long lastRestDetectTimeMs = 0;

// Visited mask (00/01/10/11)
uint8_t visitedMask = 0;

unsigned long lastDetentEventTimeMs = 0;
unsigned long lastDetentInterval    = 0;
bool haveDetentInterval = false;

int  stepIndex = array_size(CFG::DETENT_STEPS) - 1;

// Output pacing
long outAccum = 0;
unsigned long lastOutTickMs = 0;

// LED pulse management
unsigned long lastLedPulseTimeMs = 0;

// Follow target (index only)
int8_t g_ledFollowIndex = CFG::LED_FOLLOWS_SWITCH_INDEX;

// Switches array
constexpr size_t NUM_SWITCHES_LOCAL = NUM_SWITCHES;
bool          buttonState    [NUM_SWITCHES_LOCAL] = {false};
bool          lastButtonRead [NUM_SWITCHES_LOCAL] = {false};
unsigned long lastButtonTime [NUM_SWITCHES_LOCAL] = {0};

// ---- Helpers ----
static inline bool isRestState(uint8_t encoded) {
  using RP = CFG::RestPolicy;
  return (CFG::REST_POLICY == RP::Hard00or11)
         ? (encoded == 0b00 || encoded == 0b11)
         : (encoded == 0b01 || encoded == 0b10);
}
static inline byte stepFromEdgeInterval(unsigned long iv) {
  for (size_t i = 0; i < array_size(CFG::EDGE_ACC_THRESHOLDS); ++i)
    if (iv < CFG::EDGE_ACC_THRESHOLDS[i]) return CFG::EDGE_ACC_STEPS[i];
  return 1;
}

// First i such that iv < th[i] (otherwise N-1). Binary search.
template <typename T, size_t N>
static inline int lower_index_first_lt(const T (&th)[N], T iv) {
  size_t lo = 0, hi = N;            // [lo, hi)
  while (lo < hi) {
    size_t mid = lo + ((hi - lo) >> 1);
    if (iv < th[mid]) hi = mid;     // Search left half
    else               lo = mid + 1;
  }
  return (lo < N) ? (int)lo : (int)(N - 1);
}
static inline int stepIndexFromDetentIv(unsigned long iv) {
  return lower_index_first_lt(CFG::DETENT_THRESHOLDS_MS, iv);
}

// Send MIDI and (optionally) LED pulse
template<bool PULSE>
static inline void sendCC_andMaybePulse(byte cc, byte value) {
  sendCC(cc, value);                // Always send (independent of LED settings)
  if (PULSE) {                      // Compile-time constant (optimized out)
    lastLedPulseTimeMs = millis();
  }
}

// Update LED state (base + pulse override)
static inline void serviceLed() {
  byte b = CFG::LED_BRIGHTNESS_OFF;
  if (g_ledFollowIndex >= 0 && (size_t)g_ledFollowIndex < NUM_SWITCHES_LOCAL) {
    b = buttonState[g_ledFollowIndex] ? CFG::LED_BRIGHTNESS_ON
                                      : CFG::LED_BRIGHTNESS_OFF;
  }
  if (CFG::LED_ACTIVITY_PULSE) {
    unsigned long now = millis();
    if ((uint32_t)(now - lastLedPulseTimeMs) < (uint32_t)CFG::LED_PULSE_MS) {
      if (CFG::LED_PULSE_BRIGHTNESS > b) b = CFG::LED_PULSE_BRIGHTNESS;
    }
  }
  analogWrite(CFG::ledPin, b);
}

// ---- Setup ----
void setup() {
  pinMode(CFG::encoderPinA, CFG::USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
  pinMode(CFG::encoderPinB, CFG::USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);

  pinMode(CFG::ledPin, OUTPUT);
  analogWrite(CFG::ledPin, CFG::LED_BRIGHTNESS_OFF); // Initially off

  for (size_t i=0;i<NUM_SWITCHES_LOCAL;i++) {
    pinMode(CFG::SWITCHES[i].pin, INPUT_PULLUP);
    lastButtonRead[i] = HIGH;
  }

  aStable = digitalRead(CFG::encoderPinA);
  bStable = digitalRead(CFG::encoderPinB);
  aIntegr = aStable ? CFG::INTEGR_MAX : 0;
  bIntegr = bStable ? CFG::INTEGR_MAX : 0;

  lastFilterUs         = micros();
  lastOutTickMs        = millis();
  lastEdgeTimeMs       = millis();
  lastRestDetectTimeMs = millis();

  visitedMask = (1u << ((aStable << 1) | bStable));

  haveEdgeInterval   = false;
  haveDetentInterval = false;
  stepIndex = array_size(CFG::DETENT_STEPS) - 1;
}

// ---- Loop ----
void loop() {
  handleEncoder();
  handleButtons();

  // Output pacing
  unsigned long now = millis();
  if ((uint32_t)(now - lastOutTickMs) >= (uint32_t)CFG::OUTPUT_TICK_MS) {
    lastOutTickMs = now;
    byte sent = 0;
    while (outAccum != 0 && sent < CFG::MAX_PER_TICK) {
      if (outAccum > 0) { sendCC_andMaybePulse<CFG::LED_ACTIVITY_PULSE>(CFG::encoderCC, 0x41); outAccum--; }
      else              { sendCC_andMaybePulse<CFG::LED_ACTIVITY_PULSE>(CFG::encoderCC, 0x3F); outAccum++; }
      sent++;
    }
    if (sent) MidiUSB.flush();
  }

  // Update LED state
  serviceLed();
}

// ---- Encoder handling ----
void handleEncoder() {
  unsigned long nowUs = micros();
  while ((uint32_t)(nowUs - lastFilterUs) >= (uint32_t)CFG::FILTER_INTERVAL_US) {
    lastFilterUs += CFG::FILTER_INTERVAL_US;

    uint8_t aRaw = digitalRead(CFG::encoderPinA);
    uint8_t bRaw = digitalRead(CFG::encoderPinB);

    if (aRaw) { if (aIntegr < CFG::INTEGR_MAX) aIntegr++; } else { if (aIntegr > 0) aIntegr--; }
    if (bRaw) { if (bIntegr < CFG::INTEGR_MAX) bIntegr++; } else { if (bIntegr > 0) bIntegr--; }

    uint8_t aNew = aStable, bNew = bStable;
    if (aIntegr == 0) aNew = 0; else if (aIntegr == CFG::INTEGR_MAX) aNew = 1;
    if (bIntegr == 0) bNew = 0; else if (bIntegr == CFG::INTEGR_MAX) bNew = 1;
    if (aNew == aStable && bNew == bStable) continue;

    int prev    = (aStable << 1) | bStable;
    int encoded = (aNew   << 1) | bNew;
    aStable = aNew; bStable = bNew;
    visitedMask |= (1u << encoded);

    int dir = 0;
    switch ((prev << 2) | encoded) {
      case 0b0001: case 0b0111: case 0b1110: case 0b1000: dir = +1; break; // CW
      case 0b0010: case 0b0100: case 0b1101: case 0b1011: dir = -1; break; // CCW
      default: dir = 0;
    }
    if (dir == 0) continue;
    if (CFG::ENCODER_INVERT) dir = -dir;

    unsigned long nowMs = millis();

    unsigned long edgeInterval = nowMs - lastEdgeTimeMs;
    if (!haveEdgeInterval) {
      haveEdgeInterval = true;
      lastEdgeTimeMs   = nowMs;
      lastEdgeInterval = 0;
    } else {
      lastEdgeTimeMs   = nowMs;
      lastEdgeInterval = edgeInterval;
      if (!slowMode && lastEdgeInterval >= CFG::SLOW_ENTER_MS) slowMode = true;
      else if (slowMode && lastEdgeInterval <= CFG::SLOW_EXIT_MS) slowMode = false;
    }

    if (dir != lastDirection) {
      if ((uint32_t)(nowMs - lastDirectionChangeTime) < (uint32_t)CFG::ENCODER_DIRECTION_CHANGE_SUPPRESS_MS) {
        continue;
      }
      lastDirection = dir;
      lastDirectionChangeTime = nowMs;
      cycleAccum = 0;
      visitedMask = (1u << encoded); // Restart from the current state
    }

    // Gate: return-to-rest
    cycleAccum += dir;

    const bool isRest = isRestState(encoded);

    using RP = CFG::RestPolicy;
    const uint8_t nonRestMask = (CFG::REST_POLICY == RP::Hard00or11) ? 0b0110 : 0b1001; // 01|10 or 00|11
    const bool visitedEither = (visitedMask & nonRestMask) != 0;
    const bool visitedBoth   = (visitedMask & nonRestMask) == nonRestMask;
    const bool gateOK        = CFG::FULL_CYCLE_GATING ? visitedBoth : visitedEither;

    if (isRest && gateOK &&
        (uint32_t)(nowMs - lastRestDetectTimeMs) >= (uint32_t)CFG::REST_DEBOUNCE_MS) {
      lastRestDetectTimeMs = nowMs;

      if (cycleAccum != 0) {
        int outDir = (cycleAccum > 0) ? +1 : -1;

        if ((uint32_t)(nowMs - lastMoveTime) >= (uint32_t)CFG::ENCODER_STABLE_DELAY_MS) {
          if (haveDetentInterval) lastDetentInterval = nowMs - lastDetentEventTimeMs;
          lastDetentEventTimeMs = nowMs;
          haveDetentInterval = true;

          lastMoveTime = nowMs;

          byte step = 1;
          if (!slowMode) {
            if (CFG::USE_DETENT_ACCELERATION && haveDetentInterval) {
              int cand = stepIndexFromDetentIv(lastDetentInterval);
              if (cand < stepIndex) {
                if (lastDetentInterval + CFG::STEP_HYST_UP_MS < CFG::DETENT_THRESHOLDS_MS[cand]) stepIndex = cand;
              } else if (cand > stepIndex) {
                unsigned long base = CFG::DETENT_THRESHOLDS_MS[stepIndex];
                if (lastDetentInterval >= base + CFG::STEP_HYST_DOWN_MS) stepIndex = cand;
              }
              step = CFG::DETENT_STEPS[stepIndex];
            } else if (haveEdgeInterval) {
              step = stepFromEdgeInterval(lastEdgeInterval);
            }
          }

          if (slowMode && haveEdgeInterval &&
              lastEdgeInterval >= CFG::IMMEDIATE_OUTPUT_THRESHOLD_MS && outAccum == 0) {
            byte v = (outDir > 0) ? (byte)0x41 : (byte)0x3F; // Binary Offset +/-1
            for (byte i = 0; i < step; ++i) {
              sendCC_andMaybePulse<CFG::LED_ACTIVITY_PULSE>(CFG::encoderCC, v);
            }
            MidiUSB.flush();
          } else {
            outAccum += (outDir > 0) ? step : -((long)step);
            if (outAccum >  CFG::OUTACCUM_SOFT_LIMIT) outAccum =  CFG::OUTACCUM_SOFT_LIMIT;
            if (outAccum < -CFG::OUTACCUM_SOFT_LIMIT) outAccum = -CFG::OUTACCUM_SOFT_LIMIT;
          }
        }
      }
      visitedMask = (1u << encoded); // Next cycle
      cycleAccum = 0;
    }
  }
}

// ---- Switches (array-enabled) ----
void handleButtons() {
  unsigned long now = millis();
  for (size_t i = 0; i < NUM_SWITCHES_LOCAL; i++) {
    bool reading = digitalRead(CFG::SWITCHES[i].pin);
    // Toggle on rising edge (press = LOW -> release = HIGH)
    if (reading == HIGH && lastButtonRead[i] == LOW &&
        (uint32_t)(now - lastButtonTime[i]) >= (uint32_t)CFG::BUTTON_DEBOUNCE_MS) {
      lastButtonTime[i] = now;
      buttonState[i] = !buttonState[i];
      byte value = buttonState[i] ? 127 : 0;
      sendCC_andMaybePulse<CFG::LED_ACTIVITY_PULSE>(CFG::SWITCHES[i].cc, value);
      MidiUSB.flush();
    }
    lastButtonRead[i] = reading;
  }
}
