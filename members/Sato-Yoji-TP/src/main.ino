#include <Arduino.h>

// --- 詳細設計書ベース・Red LED/1桁7セグ除外 ---

struct DisplayData {
  uint8_t d0, d1, d2, d3;
};

// 分岐到達ログ（初回のみ出力）
const uint8_t BRANCH_COUNT = 48;
bool branchSeen[BRANCH_COUNT] = {false};

void markBranch(uint8_t id, const __FlashStringHelper* label) {
  if (id >= BRANCH_COUNT) return;
  if (branchSeen[id]) return;
  branchSeen[id] = true;
  Serial.print(F("[COVER] "));
  Serial.print(id);
  Serial.print(F(": "));
  Serial.println(label);
}

// ピン定義
const uint8_t PIN_BUTTON = 2;
const uint8_t PIN_ULTRASONIC_TRIG = 3;
const uint8_t PIN_ULTRASONIC_ECHO = 4;
const uint8_t PIN_7SEG_SER = 11;
const uint8_t PIN_7SEG_RCLK = 12;
const uint8_t PIN_7SEG_SRCLK = 13;
const uint8_t PIN_7SEG_DIGIT1 = 5;
const uint8_t PIN_7SEG_DIGIT2 = 6;
const uint8_t PIN_7SEG_DIGIT3 = 7;
const uint8_t PIN_7SEG_DIGIT4 = 8;

// 状態定数
const uint8_t STATE_STOP_RESET = 0;
const uint8_t STATE_RUNNING = 1;
const uint8_t STATE_STOP_HOLD = 2;
const uint8_t STATE_MAX_HOLD = 3;

// 定数
const unsigned long DEBOUNCE_DELAY_MS = 30;
const unsigned long LONG_PRESS_MS = 1000;
const unsigned long SENSOR_INTERVAL_MS = 50;
// Temporary debug setting: relaxed ultrasonic thresholds
const unsigned long SENSOR_DEADTIME_MS = 100; // 300
const uint16_t SENSOR_MIN_CM = 2; //3
const uint16_t SENSOR_MAX_CM = 20; //5
const uint8_t SENSOR_HIT_REQUIRED = 1; //2
const unsigned long TICK_INTERVAL_MS = 10;
const unsigned long DISPLAY_MUX_INTERVAL_MS = 2;
const uint16_t MAX_ELAPSED_CS = 5999;

const bool SENSOR_DEBUG_STREAM = true;
const unsigned long SENSOR_DEBUG_INTERVAL_MS = 200;

// イベント定数
const uint8_t EVENT_NONE = 0;
const uint8_t EVENT_BUTTON_SHORT = 1;
const uint8_t EVENT_BUTTON_LONG = 2;
const uint8_t EVENT_SENSOR_TRIGGER = 3;

// 7セグメントマップ（共通カソード前提）
const uint8_t SEG_MAP[10] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

// グローバル変数
uint8_t currentState = STATE_STOP_RESET;
uint16_t elapsedCs = 0;
uint16_t holdCs = 0;
bool buttonStable = false;
bool lastButtonRaw = false;
unsigned long buttonPressStartMs = 0;
unsigned long lastButtonChangeMs = 0;
uint16_t distanceCm = 999;
uint8_t sensorConsecutiveHit = 0;
unsigned long lastSensorReadMs = 0;
unsigned long lastSensorTriggerMs = 0;
unsigned long lastTickMs = 0;
unsigned long lastDisplayMuxMs = 0;
uint8_t currentDigitIndex = 0;
bool isButtonEvent = false;
bool isSensorEvent = false;
uint8_t inputEvent = EVENT_NONE;

//確認用
unsigned long lastSensorDebugMs = 0;

// 桁全消灯
void allDigitsOff() {
  digitalWrite(PIN_7SEG_DIGIT1, LOW);
  digitalWrite(PIN_7SEG_DIGIT2, LOW);
  digitalWrite(PIN_7SEG_DIGIT3, LOW);
  digitalWrite(PIN_7SEG_DIGIT4, LOW);
}

// セグメントデータ出力
void latchSegment(uint8_t value) {
  digitalWrite(PIN_7SEG_RCLK, LOW);
  shiftOut(PIN_7SEG_SER, PIN_7SEG_SRCLK, MSBFIRST, value);
  digitalWrite(PIN_7SEG_RCLK, HIGH);
}

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);
  pinMode(PIN_7SEG_SER, OUTPUT);
  pinMode(PIN_7SEG_RCLK, OUTPUT);
  pinMode(PIN_7SEG_SRCLK, OUTPUT);
  pinMode(PIN_7SEG_DIGIT1, OUTPUT);
  pinMode(PIN_7SEG_DIGIT2, OUTPUT);
  pinMode(PIN_7SEG_DIGIT3, OUTPUT);
  pinMode(PIN_7SEG_DIGIT4, OUTPUT);

  currentState = STATE_STOP_RESET;
  elapsedCs = 0;
  holdCs = 0;
  buttonStable = false;
  lastButtonRaw = false;
  buttonPressStartMs = 0;
  lastButtonChangeMs = 0;
  distanceCm = 999;
  sensorConsecutiveHit = 0;
  lastSensorReadMs = 0;
  lastSensorTriggerMs = 0;
  lastTickMs = millis();
  lastDisplayMuxMs = 0;
  currentDigitIndex = 0;
  isButtonEvent = false;
  isSensorEvent = false;
  inputEvent = EVENT_NONE;

  allDigitsOff();
  latchSegment(0x00);
  Serial.begin(9600);
  markBranch(0, F("setup: initialized"));
}

void loop() {
  // 表示を優先して更新
  updateDisplay();

  uint8_t buttonEvent = readButtonEdge();
  uint8_t sensorEvent = readUltrasonicEvent();

  isButtonEvent = (buttonEvent == EVENT_BUTTON_SHORT || buttonEvent == EVENT_BUTTON_LONG);
  isSensorEvent = (sensorEvent == EVENT_SENSOR_TRIGGER);

  inputEvent = EVENT_NONE;
  if (isButtonEvent) {
    markBranch(1, F("loop: button event prioritized"));
    inputEvent = buttonEvent;
  } else if (isSensorEvent) {
    markBranch(2, F("loop: sensor event used"));
    inputEvent = sensorEvent;
  } else {
    markBranch(3, F("loop: no input event"));
  }

  updateStateMachine(inputEvent);
  updateElapsedTime();
  updateDisplay();
}

uint8_t readButtonEdge() {
  unsigned long now = millis();
  bool rawPressed = (digitalRead(PIN_BUTTON) == LOW);
  if (rawPressed != lastButtonRaw) {
    markBranch(4, F("readButtonEdge: raw changed"));
    lastButtonRaw = rawPressed;
    lastButtonChangeMs = now;
  }
  if ((now - lastButtonChangeMs) < DEBOUNCE_DELAY_MS) {
    markBranch(5, F("readButtonEdge: debounce wait"));
    return EVENT_NONE;
  }
  if (rawPressed != buttonStable) {
    markBranch(6, F("readButtonEdge: stable changed"));
    buttonStable = rawPressed;
    if (buttonStable) {
      markBranch(7, F("readButtonEdge: press edge"));
      buttonPressStartMs = now;
    } else {
      markBranch(8, F("readButtonEdge: release edge"));
      unsigned long pressedMs = now - buttonPressStartMs;
      if (pressedMs >= LONG_PRESS_MS) {
        markBranch(9, F("readButtonEdge: long press"));
        return EVENT_BUTTON_LONG;
      }
      if (pressedMs > 0) {
        markBranch(10, F("readButtonEdge: short press"));
        return EVENT_BUTTON_SHORT;
      }
      markBranch(11, F("readButtonEdge: zero-length release"));
    }
  }
  markBranch(12, F("readButtonEdge: no event"));
  return EVENT_NONE;
}

uint8_t readUltrasonicEvent() {
  unsigned long now = millis();
  if ((now - lastSensorReadMs) < SENSOR_INTERVAL_MS) {
    markBranch(13, F("readUltrasonicEvent: interval wait"));
    return EVENT_NONE;
  }
  lastSensorReadMs = now;
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  unsigned long pulseUs = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, 4000);
  if (pulseUs == 0) {
    markBranch(14, F("readUltrasonicEvent: timeout"));
    distanceCm = 999;
    sensorConsecutiveHit = 0;
    return EVENT_NONE;
  }
  distanceCm = (uint16_t)(pulseUs / 58UL);

  if (SENSOR_DEBUG_STREAM && (now - lastSensorDebugMs) >= SENSOR_DEBUG_INTERVAL_MS) {
    lastSensorDebugMs = now;
    Serial.print(F("[SENSOR] pulseUs="));
    Serial.print(pulseUs);
    Serial.print(F(" distanceCm="));
    Serial.print(distanceCm);
    Serial.print(F(" hit="));
    Serial.print(sensorConsecutiveHit);
    Serial.print(F(" state="));
    Serial.println(currentState);
  }

  if (distanceCm == 0 || distanceCm > 400) {
    markBranch(15, F("readUltrasonicEvent: invalid distance"));
    sensorConsecutiveHit = 0;
    return EVENT_NONE;
  }
  if (distanceCm >= SENSOR_MIN_CM && distanceCm <= SENSOR_MAX_CM) {
    markBranch(16, F("readUltrasonicEvent: in range"));
    if (sensorConsecutiveHit < 255) sensorConsecutiveHit++;
  } else {
    markBranch(17, F("readUltrasonicEvent: out of range"));
    sensorConsecutiveHit = 0;
  }
  if (sensorConsecutiveHit >= SENSOR_HIT_REQUIRED) {
    markBranch(18, F("readUltrasonicEvent: hit threshold"));
    if ((now - lastSensorTriggerMs) >= SENSOR_DEADTIME_MS) {
      markBranch(19, F("readUltrasonicEvent: trigger accepted"));
      sensorConsecutiveHit = 0;
      lastSensorTriggerMs = now;
      return EVENT_SENSOR_TRIGGER;
    }
    markBranch(20, F("readUltrasonicEvent: deadtime blocking"));
  }
  markBranch(21, F("readUltrasonicEvent: no event"));
  return EVENT_NONE;
}

void updateStateMachine(uint8_t event) {
  if (event == EVENT_NONE) {
    markBranch(22, F("updateStateMachine: no event"));
    return;
  }
  if (event == EVENT_BUTTON_LONG) {
    markBranch(23, F("updateStateMachine: long press reset"));
    resetStopwatch();
    return;
  }
  markBranch(24, F("updateStateMachine: start/stop route"));
  triggerStartStop(event);
}

void triggerStartStop(uint8_t event) {
  if (event != EVENT_BUTTON_SHORT && event != EVENT_SENSOR_TRIGGER) {
    markBranch(25, F("triggerStartStop: ignored event"));
    return;
  }
  unsigned long now = millis();
  if (currentState == STATE_STOP_RESET || currentState == STATE_STOP_HOLD) {
    markBranch(26, F("triggerStartStop: enter running"));
    currentState = STATE_RUNNING;
    lastTickMs = now;
    lastSensorTriggerMs = now;
    return;
  }
  if (currentState == STATE_RUNNING) {
    markBranch(27, F("triggerStartStop: capture hold"));
    captureAndHoldTime();
    currentState = STATE_STOP_HOLD;
    lastSensorTriggerMs = now;
    return;
  }
  markBranch(28, F("triggerStartStop: max-hold ignored"));
  // STATE_MAX_HOLDはリセットのみ許可
}

void updateElapsedTime() {
  if (currentState != STATE_RUNNING) {
    markBranch(29, F("updateElapsedTime: not running"));
    return;
  }
  unsigned long now = millis();
  bool progressed = false;
  while ((now - lastTickMs) >= TICK_INTERVAL_MS) {
    progressed = true;
    if (elapsedCs < MAX_ELAPSED_CS) {
      markBranch(30, F("updateElapsedTime: tick increment"));
      elapsedCs++;
      lastTickMs += TICK_INTERVAL_MS;
    } else {
      markBranch(31, F("updateElapsedTime: reached max"));
      elapsedCs = MAX_ELAPSED_CS;
      holdCs = MAX_ELAPSED_CS;
      currentState = STATE_MAX_HOLD;
      break;
    }
  }
  if (!progressed) {
    markBranch(32, F("updateElapsedTime: waiting next tick"));
  }
}

DisplayData formatMainDisplay(uint16_t cs) {
  if (cs > MAX_ELAPSED_CS) {
    markBranch(33, F("formatMainDisplay: clamped"));
    cs = MAX_ELAPSED_CS;
  }
  uint8_t sec = cs / 100;
  uint8_t centi = cs % 100;
  DisplayData data;
  data.d0 = sec / 10;
  data.d1 = sec % 10;
  data.d2 = centi / 10;
  data.d3 = centi % 10;
  return data;
}

void updateDisplay() {
  unsigned long now = millis();
  if ((now - lastDisplayMuxMs) < DISPLAY_MUX_INTERVAL_MS) {
    markBranch(34, F("updateDisplay: mux wait"));
    return;
  }
  lastDisplayMuxMs = now;
  uint16_t displayCs = 0;
  if (currentState == STATE_RUNNING) {
    markBranch(35, F("updateDisplay: show elapsed"));
    displayCs = elapsedCs;
  } else if (currentState == STATE_STOP_HOLD) {
    markBranch(36, F("updateDisplay: show hold"));
    displayCs = holdCs;
  } else if (currentState == STATE_MAX_HOLD) {
    markBranch(37, F("updateDisplay: show max"));
    displayCs = MAX_ELAPSED_CS;
  } else {
    markBranch(38, F("updateDisplay: show zero/reset"));
  }
  const DisplayData data = formatMainDisplay(displayCs);
  const uint8_t digits[4] = {data.d0, data.d1, data.d2, data.d3};
  allDigitsOff();
  uint8_t seg = SEG_MAP[digits[currentDigitIndex]];
  if (currentDigitIndex == 1) {
    markBranch(39, F("updateDisplay: decimal point on"));
    seg |= 0b10000000;
  }
  latchSegment(seg);
  switch (currentDigitIndex) {
    case 0:
      markBranch(40, F("updateDisplay: digit1"));
      digitalWrite(PIN_7SEG_DIGIT1, HIGH);
      break;
    case 1:
      markBranch(41, F("updateDisplay: digit2"));
      digitalWrite(PIN_7SEG_DIGIT2, HIGH);
      break;
    case 2:
      markBranch(42, F("updateDisplay: digit3"));
      digitalWrite(PIN_7SEG_DIGIT3, HIGH);
      break;
    default:
      markBranch(43, F("updateDisplay: digit4"));
      digitalWrite(PIN_7SEG_DIGIT4, HIGH);
      break;
  }
  currentDigitIndex = (currentDigitIndex + 1) % 4;
}

void captureAndHoldTime() {
  markBranch(44, F("captureAndHoldTime: captured"));
  holdCs = elapsedCs;
}

void resetStopwatch() {
  markBranch(45, F("resetStopwatch: reset values"));
  elapsedCs = 0;
  holdCs = 0;
  currentState = STATE_STOP_RESET;
  sensorConsecutiveHit = 0;
  inputEvent = EVENT_NONE;
  isButtonEvent = false;
  isSensorEvent = false;
  lastTickMs = millis();
}
