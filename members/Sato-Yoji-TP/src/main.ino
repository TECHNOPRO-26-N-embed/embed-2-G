#include <Arduino.h>

// Pin definitions
//
// --- 4桁7セグメント表示器（共通カソード、12ピン型の例） ---
//  Arduino D5  → DIG1（左端桁）
//  Arduino D6  → DIG2
//  Arduino D7  → DIG3
//  Arduino D8  → DIG4（右端桁）
//  74HC595 Q0～Q7 → a,b,c,d,e,f,g,dp（各セグメント）
//
// --- 74HC595 ---
//  Arduino D11 → SER（DS）
//  Arduino D12 → RCLK（ST_CP、ラッチ）
//  Arduino D13 → SRCLK（SH_CP、クロック）
//  VCC/GND/MR/OEは固定配線
//
// --- ボタン ---
//  Arduino D2  → タクトスイッチ片側（もう片側はGND）
//
// --- 超音波センサ（HC-SR04等） ---
//  Arduino D3  → TRIG
//  Arduino D4  → ECHO
//  VCC/GNDは5V/GNDへ
//
// --- 未使用ピン（この構成では配線不要） ---
//  Arduino D9  → 1桁7セグ（分表示）
//  Arduino D10 → 赤LED
//
// ※セグメントa～g,dpの物理ピン番号は部品型番のデータシート参照
// ※安全のため各セグメント線に220Ω程度の抵抗を推奨
static const uint8_t PIN_BUTTON = 2;
static const uint8_t PIN_ULTRASONIC_TRIG = 3;
static const uint8_t PIN_ULTRASONIC_ECHO = 4;
static const uint8_t PIN_7SEG_DIGIT1 = 5;
static const uint8_t PIN_7SEG_DIGIT2 = 6;
static const uint8_t PIN_7SEG_DIGIT3 = 7;
static const uint8_t PIN_7SEG_DIGIT4 = 8;

static const uint8_t PIN_7SEG_MINUTE = 9;
static const uint8_t PIN_LED_RED = 10;
static const uint8_t PIN_7SEG_SER = 11;
static const uint8_t PIN_7SEG_RCLK = 12;
static const uint8_t PIN_7SEG_SRCLK = 13;

// State constants
static const uint8_t STATE_STOP_RESET = 0;
static const uint8_t STATE_RUNNING = 1;
static const uint8_t STATE_STOP_HOLD = 2;
static const uint8_t STATE_MAX_HOLD = 3;

// Timing / threshold constants
static const unsigned long DEBOUNCE_DELAY_MS = 30;
static const unsigned long LONG_PRESS_MS = 1000;
static const unsigned long SENSOR_INTERVAL_MS = 50;
static const unsigned long SENSOR_DEADTIME_MS = 300;
static const uint16_t SENSOR_MIN_CM = 3;
static const uint16_t SENSOR_MAX_CM = 5;
static const uint8_t SENSOR_HIT_REQUIRED = 2;
static const unsigned long TICK_INTERVAL_MS = 10;
static const uint16_t MAX_ELAPSED_CS = 5999;
static const unsigned long LED_PULSE_MS = 1000;
static const unsigned long DISPLAY_MUX_INTERVAL_MS = 2;

// Input event constants
static const uint8_t EVENT_NONE = 0;
static const uint8_t EVENT_BUTTON_SHORT = 1;
static const uint8_t EVENT_BUTTON_LONG = 2;
static const uint8_t EVENT_SENSOR_TRIGGER = 3;

struct DisplayData {
  uint8_t d0;
  uint8_t d1;
  uint8_t d2;
  uint8_t d3;
};

// Global state variables
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
unsigned long ledOffAtMs = 0;
bool minuteOptional = false;

bool isButtonEvent = false;
bool isSensorEvent = false;
uint8_t inputEvent = EVENT_NONE;

unsigned long lastDisplayMuxMs = 0;
uint8_t currentDigitIndex = 0;
bool startupLedDone = false;

// 7-seg (common cathode, bit0..6 = a..g, bit7 = dp)
static const uint8_t SEG_MAP[10] = {
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

uint8_t readButtonEdge();
uint8_t readUltrasonicEvent();
void updateStateMachine(uint8_t event);
void updateElapsedTime();
void updateDisplay();
void updateLedPulse();
DisplayData formatMainDisplay(uint16_t cs);
void triggerStartStop(uint8_t event);
void captureAndHoldTime();
void resetStopwatch();
void updateMinuteDisplay(uint16_t cs);
void pulseLedOnTrigger();

static const bool TRACE_BRANCH_ENABLED = true;

static void traceBranch(const __FlashStringHelper* functionName, const __FlashStringHelper* branchName) {
  if (!TRACE_BRANCH_ENABLED) {
    return;
  }
  Serial.print(F("[TRACE] "));
  Serial.print(functionName);
  Serial.print(F(" -> "));
  Serial.println(branchName);
}

static void traceBranchOnChange(int8_t& lastCode, int8_t newCode,
                                const __FlashStringHelper* functionName,
                                const __FlashStringHelper* branchName) {
  if (!TRACE_BRANCH_ENABLED) {
    return;
  }
  if (lastCode != newCode) {
    lastCode = newCode;
    traceBranch(functionName, branchName);
  }
}

// すべての7セグ桁選択をOFFにして多重化のゴースト表示を防ぎます。
static void allDigitsOff() {
  digitalWrite(PIN_7SEG_DIGIT1, LOW);
  digitalWrite(PIN_7SEG_DIGIT2, LOW);
  digitalWrite(PIN_7SEG_DIGIT3, LOW);
  digitalWrite(PIN_7SEG_DIGIT4, LOW);
  digitalWrite(PIN_7SEG_MINUTE, LOW);
}

// 74HC595へ1バイトのセグメントデータを送ってラッチします。
static void latchSegment(uint8_t value) {
  digitalWrite(PIN_7SEG_RCLK, LOW);
  shiftOut(PIN_7SEG_SER, PIN_7SEG_SRCLK, MSBFIRST, value);
  digitalWrite(PIN_7SEG_RCLK, HIGH);
}

// ピン初期化と各種状態変数の初期値設定を行います。
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
  pinMode(PIN_7SEG_MINUTE, OUTPUT);

  pinMode(PIN_LED_RED, OUTPUT);

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
  ledOffAtMs = 0;
  isButtonEvent = false;
  isSensorEvent = false;
  inputEvent = EVENT_NONE;
  lastDisplayMuxMs = 0;
  currentDigitIndex = 0;

  allDigitsOff();
  latchSegment(0x00);

  Serial.begin(9600);
  traceBranch(F("setup"), F("serial-begin"));

  digitalWrite(PIN_LED_RED, HIGH);
  traceBranch(F("setup"), F("startup-led-on"));
  delay(1000);
  digitalWrite(PIN_LED_RED, LOW);
  traceBranch(F("setup"), F("startup-led-off"));
  startupLedDone = true;
  traceBranch(F("setup"), F("startup-complete"));
}

// 入力取得・状態更新・表示更新・LED更新を毎ループ実行します。
void loop() {
  static int8_t traceCode = -1;

  const uint8_t buttonEvent = readButtonEdge();
  const uint8_t sensorEvent = readUltrasonicEvent();

  isButtonEvent = (buttonEvent == EVENT_BUTTON_SHORT || buttonEvent == EVENT_BUTTON_LONG);
  isSensorEvent = (sensorEvent == EVENT_SENSOR_TRIGGER);

  inputEvent = EVENT_NONE;
  if (isButtonEvent) {
    traceBranchOnChange(traceCode, 0, F("loop"), F("button-priority"));
    inputEvent = buttonEvent;
    isSensorEvent = false;
  } else if (isSensorEvent) {
    traceBranchOnChange(traceCode, 1, F("loop"), F("sensor-adopted"));
    inputEvent = sensorEvent;
  } else {
    traceBranchOnChange(traceCode, 2, F("loop"), F("no-event"));
  }

  updateStateMachine(inputEvent);
  updateElapsedTime();
  updateDisplay();
  updateLedPulse();
}

// デバウンス後の押下/離上から短押しまたは長押しイベントを生成します。
uint8_t readButtonEdge() {
  static int8_t traceCode = -1;

  const unsigned long now = millis();
  const bool rawPressed = (digitalRead(PIN_BUTTON) == LOW);

  if (rawPressed != lastButtonRaw) {
    traceBranchOnChange(traceCode, 0, F("readButtonEdge"), F("raw-state-changed"));
    lastButtonRaw = rawPressed;
    lastButtonChangeMs = now;
  } else {
    traceBranchOnChange(traceCode, 1, F("readButtonEdge"), F("raw-state-unchanged"));
  }

  if ((now - lastButtonChangeMs) < DEBOUNCE_DELAY_MS) {
    traceBranchOnChange(traceCode, 2, F("readButtonEdge"), F("debounce-ignore"));
    return EVENT_NONE;
  } else {
    traceBranchOnChange(traceCode, 3, F("readButtonEdge"), F("debounce-confirm-window"));
  }

  if (rawPressed != buttonStable) {
    traceBranchOnChange(traceCode, 4, F("readButtonEdge"), F("stable-state-updated"));
    buttonStable = rawPressed;

    if (buttonStable) {
      traceBranchOnChange(traceCode, 5, F("readButtonEdge"), F("press-start"));
      buttonPressStartMs = now;
    } else {
      traceBranchOnChange(traceCode, 6, F("readButtonEdge"), F("release-detected"));
      const unsigned long pressedMs = now - buttonPressStartMs;
      if (pressedMs >= LONG_PRESS_MS) {
        traceBranchOnChange(traceCode, 7, F("readButtonEdge"), F("long-press-event"));
        return EVENT_BUTTON_LONG;
      }
      if (pressedMs > 0) {
        traceBranchOnChange(traceCode, 8, F("readButtonEdge"), F("short-press-event"));
        return EVENT_BUTTON_SHORT;
      } else {
        traceBranchOnChange(traceCode, 9, F("readButtonEdge"), F("zero-duration-ignore"));
      }
    }
  } else {
    traceBranchOnChange(traceCode, 10, F("readButtonEdge"), F("stable-state-nochange"));
  }

  traceBranchOnChange(traceCode, 11, F("readButtonEdge"), F("return-none"));
  return EVENT_NONE;
}

// 超音波距離を周期測定し閾値内の連続一致からセンサイベントを生成します。
uint8_t readUltrasonicEvent() {
  static int8_t traceCode = -1;

  const unsigned long now = millis();

  if ((now - lastSensorReadMs) < SENSOR_INTERVAL_MS) {
    traceBranchOnChange(traceCode, 0, F("readUltrasonicEvent"), F("interval-skip"));
    return EVENT_NONE;
  } else {
    traceBranchOnChange(traceCode, 1, F("readUltrasonicEvent"), F("interval-read"));
  }
  lastSensorReadMs = now;

  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  const unsigned long pulseUs = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, 30000UL);
  if (pulseUs == 0) {
    traceBranchOnChange(traceCode, 2, F("readUltrasonicEvent"), F("pulse-timeout"));
    distanceCm = 999;
    sensorConsecutiveHit = 0;
    return EVENT_NONE;
  } else {
    traceBranchOnChange(traceCode, 3, F("readUltrasonicEvent"), F("pulse-ok"));
  }

  distanceCm = static_cast<uint16_t>(pulseUs / 58UL);
  if (distanceCm == 0 || distanceCm > 400) {
    traceBranchOnChange(traceCode, 4, F("readUltrasonicEvent"), F("distance-invalid"));
    sensorConsecutiveHit = 0;
    return EVENT_NONE;
  } else {
    traceBranchOnChange(traceCode, 5, F("readUltrasonicEvent"), F("distance-valid"));
  }

  if (distanceCm >= SENSOR_MIN_CM && distanceCm <= SENSOR_MAX_CM) {
    traceBranchOnChange(traceCode, 6, F("readUltrasonicEvent"), F("distance-in-range"));
    if (sensorConsecutiveHit < 255) {
      traceBranchOnChange(traceCode, 7, F("readUltrasonicEvent"), F("hit-count-increment"));
      sensorConsecutiveHit++;
    } else {
      traceBranchOnChange(traceCode, 8, F("readUltrasonicEvent"), F("hit-count-saturated"));
    }
  } else {
    traceBranchOnChange(traceCode, 9, F("readUltrasonicEvent"), F("distance-out-of-range"));
    sensorConsecutiveHit = 0;
  }

  if (sensorConsecutiveHit >= SENSOR_HIT_REQUIRED) {
    traceBranchOnChange(traceCode, 10, F("readUltrasonicEvent"), F("hit-threshold-reached"));
    if ((now - lastSensorTriggerMs) >= SENSOR_DEADTIME_MS) {
      traceBranchOnChange(traceCode, 11, F("readUltrasonicEvent"), F("deadtime-passed-trigger"));
      sensorConsecutiveHit = 0;
      lastSensorTriggerMs = now;
      return EVENT_SENSOR_TRIGGER;
    } else {
      traceBranchOnChange(traceCode, 12, F("readUltrasonicEvent"), F("deadtime-blocked"));
    }
  } else {
    traceBranchOnChange(traceCode, 13, F("readUltrasonicEvent"), F("hit-threshold-not-reached"));
  }

  traceBranchOnChange(traceCode, 14, F("readUltrasonicEvent"), F("return-none"));
  return EVENT_NONE;
}

// 確定イベントに応じてリセットまたは開始停止の状態遷移処理を呼び出します。
void updateStateMachine(uint8_t event) {
  static int8_t traceCode = -1;

  if (event == EVENT_NONE) {
    traceBranchOnChange(traceCode, 0, F("updateStateMachine"), F("event-none"));
    return;
  }

  if (event == EVENT_BUTTON_LONG) {
    traceBranchOnChange(traceCode, 1, F("updateStateMachine"), F("long-press-reset"));
    resetStopwatch();
    return;
  } else {
    traceBranchOnChange(traceCode, 2, F("updateStateMachine"), F("start-stop-path"));
  }

  triggerStartStop(event);
}

// 短押しまたはセンサ入力でRUNとSTOP系状態を切り替えます。
void triggerStartStop(uint8_t event) {
  static int8_t traceCode = -1;

  if (event != EVENT_BUTTON_SHORT && event != EVENT_SENSOR_TRIGGER) {
    traceBranchOnChange(traceCode, 0, F("triggerStartStop"), F("unsupported-event"));
    return;
  } else {
    traceBranchOnChange(traceCode, 1, F("triggerStartStop"), F("supported-event"));
  }

  const unsigned long now = millis();

  if (currentState == STATE_STOP_RESET || currentState == STATE_STOP_HOLD) {
    traceBranchOnChange(traceCode, 2, F("triggerStartStop"), F("stop-to-run"));
    currentState = STATE_RUNNING;
    lastTickMs = now;
    lastSensorTriggerMs = now;
    pulseLedOnTrigger();
    return;
  }

  if (currentState == STATE_RUNNING) {
    traceBranchOnChange(traceCode, 3, F("triggerStartStop"), F("run-to-hold"));
    captureAndHoldTime();
    currentState = STATE_STOP_HOLD;
    lastSensorTriggerMs = now;
    pulseLedOnTrigger();
    return;
  }

  traceBranchOnChange(traceCode, 4, F("triggerStartStop"), F("max-hold-no-transition"));
  // In MAX_HOLD, only long press reset is accepted.
}

// RUN中のみ10ms単位で経過時間を加算し上限到達で固定停止へ遷移します。
void updateElapsedTime() {
  static int8_t traceCode = -1;

  if (currentState != STATE_RUNNING) {
    traceBranchOnChange(traceCode, 0, F("updateElapsedTime"), F("not-running-skip"));
    return;
  } else {
    traceBranchOnChange(traceCode, 1, F("updateElapsedTime"), F("running-check"));
  }

  const unsigned long now = millis();
  if ((now - lastTickMs) < TICK_INTERVAL_MS) {
    traceBranchOnChange(traceCode, 2, F("updateElapsedTime"), F("tick-not-ready"));
  }
  while ((now - lastTickMs) >= TICK_INTERVAL_MS) {
    if (elapsedCs < MAX_ELAPSED_CS) {
      traceBranchOnChange(traceCode, 3, F("updateElapsedTime"), F("increment-elapsed"));
      elapsedCs++;
      lastTickMs += TICK_INTERVAL_MS;
    } else {
      traceBranchOnChange(traceCode, 4, F("updateElapsedTime"), F("max-reached-transition"));
      elapsedCs = MAX_ELAPSED_CS;
      holdCs = MAX_ELAPSED_CS;
      currentState = STATE_MAX_HOLD;
      break;
    }
  }
}

// centisecond値をSS.hh表示用の4桁データへ分解します。
DisplayData formatMainDisplay(uint16_t cs) {
  static int8_t traceCode = -1;

  if (cs > MAX_ELAPSED_CS) {
    traceBranchOnChange(traceCode, 0, F("formatMainDisplay"), F("clamp-max"));
    cs = MAX_ELAPSED_CS;
  } else {
    traceBranchOnChange(traceCode, 1, F("formatMainDisplay"), F("no-clamp"));
  }

  const uint8_t sec = static_cast<uint8_t>(cs / 100);
  const uint8_t centi = static_cast<uint8_t>(cs % 100);

  DisplayData data;
  data.d0 = static_cast<uint8_t>(sec / 10);
  data.d1 = static_cast<uint8_t>(sec % 10);
  data.d2 = static_cast<uint8_t>(centi / 10);
  data.d3 = static_cast<uint8_t>(centi % 10);
  return data;
}

// 現在状態に応じた値を4桁7セグへ多重化表示します。
void updateDisplay() {
  static int8_t traceCode = -1;

  const unsigned long now = millis();
  if ((now - lastDisplayMuxMs) < DISPLAY_MUX_INTERVAL_MS) {
    traceBranchOnChange(traceCode, 0, F("updateDisplay"), F("mux-wait"));
    return;
  } else {
    traceBranchOnChange(traceCode, 1, F("updateDisplay"), F("mux-update"));
  }
  lastDisplayMuxMs = now;

  uint16_t displayCs = 0;
  if (currentState == STATE_RUNNING) {
    traceBranchOnChange(traceCode, 2, F("updateDisplay"), F("state-running"));
    displayCs = elapsedCs;
  } else if (currentState == STATE_STOP_HOLD) {
    traceBranchOnChange(traceCode, 3, F("updateDisplay"), F("state-stop-hold"));
    displayCs = holdCs;
  } else if (currentState == STATE_MAX_HOLD) {
    traceBranchOnChange(traceCode, 4, F("updateDisplay"), F("state-max-hold"));
    displayCs = MAX_ELAPSED_CS;
  } else {
    traceBranchOnChange(traceCode, 5, F("updateDisplay"), F("state-stop-reset"));
    displayCs = 0;
  }

  const DisplayData data = formatMainDisplay(displayCs);
  const uint8_t digits[4] = {data.d0, data.d1, data.d2, data.d3};

  allDigitsOff();

  uint8_t seg = SEG_MAP[digits[currentDigitIndex]];
  if (currentDigitIndex == 1) {
    traceBranchOnChange(traceCode, 6, F("updateDisplay"), F("dot-enabled"));
    seg |= 0b10000000;
  } else {
    traceBranchOnChange(traceCode, 7, F("updateDisplay"), F("dot-disabled"));
  }
  latchSegment(seg);

  switch (currentDigitIndex) {
    case 0:
      traceBranchOnChange(traceCode, 8, F("updateDisplay"), F("digit1-on"));
      digitalWrite(PIN_7SEG_DIGIT1, HIGH);
      break;
    case 1:
      traceBranchOnChange(traceCode, 9, F("updateDisplay"), F("digit2-on"));
      digitalWrite(PIN_7SEG_DIGIT2, HIGH);
      break;
    case 2:
      traceBranchOnChange(traceCode, 10, F("updateDisplay"), F("digit3-on"));
      digitalWrite(PIN_7SEG_DIGIT3, HIGH);
      break;
    default:
      traceBranchOnChange(traceCode, 11, F("updateDisplay"), F("digit4-on"));
      digitalWrite(PIN_7SEG_DIGIT4, HIGH);
      break;
  }

  currentDigitIndex = static_cast<uint8_t>((currentDigitIndex + 1) % 4);

  if (minuteOptional) {
    traceBranchOnChange(traceCode, 12, F("updateDisplay"), F("minute-optional-on"));
    updateMinuteDisplay(displayCs);
  } else {
    traceBranchOnChange(traceCode, 13, F("updateDisplay"), F("minute-optional-off"));
  }
}

// 60秒以上のときだけ任意の1桁7セグへ分表示を出力します。
void updateMinuteDisplay(uint16_t cs) {
  static int8_t traceCode = -1;

  if (cs < 6000) {
    traceBranchOnChange(traceCode, 0, F("updateMinuteDisplay"), F("under-60s-off"));
    digitalWrite(PIN_7SEG_MINUTE, LOW);
    return;
  } else {
    traceBranchOnChange(traceCode, 1, F("updateMinuteDisplay"), F("over-60s-on"));
  }

  const uint8_t minute = static_cast<uint8_t>((cs / 6000) % 10);

  allDigitsOff();
  latchSegment(SEG_MAP[minute]);
  digitalWrite(PIN_7SEG_MINUTE, HIGH);
}

// LED消灯予定時刻まで点灯し期限を過ぎたら消灯します。
void updateLedPulse() {
  static int8_t traceCode = -1;

  const unsigned long now = millis();
  if (now < ledOffAtMs) {
    traceBranchOnChange(traceCode, 0, F("updateLedPulse"), F("led-on"));
    digitalWrite(PIN_LED_RED, HIGH);
  } else {
    traceBranchOnChange(traceCode, 1, F("updateLedPulse"), F("led-off"));
    digitalWrite(PIN_LED_RED, LOW);
  }
}

// start/stopトリガ発生時のLED点灯期限を現在時刻から1秒後に設定します。
void pulseLedOnTrigger() {
  traceBranch(F("pulseLedOnTrigger"), F("set-off-deadline"));
  ledOffAtMs = millis() + LED_PULSE_MS;
}

// 停止遷移時の表示保持用として現在計測値を保持値へコピーします。
void captureAndHoldTime() {
  traceBranch(F("captureAndHoldTime"), F("copy-elapsed-to-hold"));
  holdCs = elapsedCs;
}

// 計測値と保持値を初期化して停止初期状態へ戻します。
void resetStopwatch() {
  traceBranch(F("resetStopwatch"), F("reset-all-counters"));
  elapsedCs = 0;
  holdCs = 0;
  currentState = STATE_STOP_RESET;
  sensorConsecutiveHit = 0;
  inputEvent = EVENT_NONE;
  isButtonEvent = false;
  isSensorEvent = false;
  lastTickMs = millis();
}
