// 手持無沙汰解消スロット - Arduinoスケッチ
// 12ピン直結型 Four Digital Seven Segment Display 対応版
#include <Arduino.h>

// ピン定義
#define PIN_STICK_ANALOG A0
#define PIN_STOP_1 3
#define PIN_STOP_2 4
#define PIN_STOP_3 5
#define PIN_LED_1 6
#define PIN_LED_2 7
#define PIN_LED_3 12

// 74HC595経由の7セグ制御ピン（Four_Digital.ino方式）
#define PIN_7SEG_LATCH 9
#define PIN_7SEG_CLOCK 10
#define PIN_7SEG_DATA 8
// 桁選択端子（左:百, 中:十, 右:一）
#define DIGIT1_PIN A3
#define DIGIT2_PIN A4
#define DIGIT3_PIN A5

// 状態定義
#define STATE_WAIT 0
#define STATE_SPINNING 1
#define STATE_STOPPING 2
#define STATE_RESULT 3

// 定数
const unsigned long DEBOUNCE_MS = 50;
const unsigned long REEL_UPDATE_MS = 50;
const unsigned long LED_CHASE_MS = 200;
const unsigned long RESULT_HOLD_MS = 1000;
const byte WIN_NUMBER = 7;
const byte REEL_COUNT = 3;
const byte MAX_DIGIT = 9;

// Four_Digital.inoと同じ7セグ表示テーブル
const uint8_t SEG_TABLE[] = {
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
  0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, 0x00
};

// グローバル変数
byte currentState = STATE_WAIT;
bool isWinning = false;
int stickYValue = 512;
bool startActivePrev = false;
unsigned long lastStartMs = 0;
unsigned long lastStop1Ms = 0;
unsigned long lastStop2Ms = 0;
unsigned long lastStop3Ms = 0;
unsigned long lastReelMs = 0;
unsigned long lastLedMs = 0;
unsigned long resultEndMs = 0;
byte ledIndex = 0;
byte reels[REEL_COUNT] = {0, 0, 0};
bool fixed[REEL_COUNT] = {false, false, false};
byte stopCount = 0;
int displayDigits[3] = {0, 0, 0};
volatile byte muxDigitIndex = 0;

// 入力状態構造体
struct InputState {
  bool startPressed;
  bool stop1Pressed;
  bool stop2Pressed;
  bool stop3Pressed;
};

void setDisplayInt(int num) {
  if (num > 999) num = 999;
  if (num < 0) num = 0;
  displayDigits[0] = num / 100;
  displayDigits[1] = (num / 10) % 10;
  displayDigits[2] = num % 10;
}

void displayDigit(uint8_t num) {
  if (num > 16) return;
  digitalWrite(PIN_7SEG_LATCH, LOW);
  shiftOut(PIN_7SEG_DATA, PIN_7SEG_CLOCK, MSBFIRST, SEG_TABLE[num]);
  digitalWrite(PIN_7SEG_LATCH, HIGH);
}

void setAllDigitsOff() {
  digitalWrite(DIGIT1_PIN, LOW);
  digitalWrite(DIGIT2_PIN, LOW);
  digitalWrite(DIGIT3_PIN, LOW);
}

void refreshDisplay() {
  setAllDigitsOff();
  displayDigit(displayDigits[muxDigitIndex]);
  switch(muxDigitIndex) {
    case 0: digitalWrite(DIGIT1_PIN, HIGH); break;
    case 1: digitalWrite(DIGIT2_PIN, HIGH); break;
    case 2: digitalWrite(DIGIT3_PIN, HIGH); break;
  }
  muxDigitIndex = (muxDigitIndex + 1) % 3;
}

byte prevState = 255;

const char* stateName(byte state) {
  switch(state) {
    case STATE_WAIT: return "WAIT";
    case STATE_SPINNING: return "SPINNING";
    case STATE_STOPPING: return "STOPPING";
    case STATE_RESULT: return "RESULT";
    default: return "UNKNOWN";
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_STICK_ANALOG, INPUT);
  pinMode(PIN_STOP_1, INPUT_PULLUP);
  pinMode(PIN_STOP_2, INPUT_PULLUP);
  pinMode(PIN_STOP_3, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);
  pinMode(PIN_7SEG_LATCH, OUTPUT);
  pinMode(PIN_7SEG_CLOCK, OUTPUT);
  pinMode(PIN_7SEG_DATA, OUTPUT);

  pinMode(DIGIT1_PIN, OUTPUT);
  pinMode(DIGIT2_PIN, OUTPUT);
  pinMode(DIGIT3_PIN, OUTPUT);
  setAllDigitsOff();

  randomSeed(analogRead(0));
  currentState = STATE_WAIT;
  for (int i = 0; i < REEL_COUNT; i++) {
    reels[i] = 0;
    fixed[i] = false;
  }
  stopCount = 0;
  setDisplayInt(0);

  // LED初期演出
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_1 + i, HIGH);
    delay(LED_CHASE_MS);
    digitalWrite(PIN_LED_1 + i, LOW);
  }
}

void loop() {
  unsigned long now = millis();
  refreshDisplay();

  InputState inputs = readInputs();
  updateEffects(now);

  if (currentState != prevState) {
    Serial.print("State: ");
    Serial.println(stateName(currentState));
    prevState = currentState;
  }
  switch (currentState) {
    case STATE_WAIT:
      setDisplayInt(0);
      if (handleStartInput(inputs, now)) {
        isWinning = (random(0, 30) == 0);
        stopCount = 0;
        for (int i = 0; i < REEL_COUNT; i++) {
          fixed[i] = false;
        }
        currentState = STATE_SPINNING;
      }
      break;

    case STATE_SPINNING:
      updateReelsAndDisplay(now);
      if (handleStopInput(inputs, now)) {
        currentState = (stopCount == REEL_COUNT) ? STATE_STOPPING : STATE_SPINNING;
      }
      break;

    case STATE_STOPPING:
      updateReelsAndDisplay(now);
      if (stopCount == REEL_COUNT) {
        runResultLighting(now);
        currentState = STATE_RESULT;
      }
      break;

    case STATE_RESULT:
      if (now >= resultEndMs) {
        for (int i = 0; i < 3; i++) {
          digitalWrite(PIN_LED_1 + i, LOW);
        }
        currentState = STATE_WAIT;
      }
      break;
  }
}

InputState readInputs() {
  InputState state;
  stickYValue = analogRead(PIN_STICK_ANALOG);
  bool startActiveNow = (stickYValue <= 800);
  state.startPressed = (startActiveNow && !startActivePrev);
  startActivePrev = startActiveNow;
  state.stop1Pressed = (digitalRead(PIN_STOP_1) == LOW);
  state.stop2Pressed = (digitalRead(PIN_STOP_2) == LOW);
  state.stop3Pressed = (digitalRead(PIN_STOP_3) == LOW);
  return state;
}

bool handleStartInput(const InputState &inputs, unsigned long now) {
  if (!inputs.startPressed || (now - lastStartMs < DEBOUNCE_MS)) {
    return false;
  }
  lastStartMs = now;
  return true;
}

bool handleStopInput(const InputState &inputs, unsigned long now) {
  bool anyStopped = false;
  if (inputs.stop1Pressed && !fixed[0] && (now - lastStop1Ms >= DEBOUNCE_MS)) {
    lastStop1Ms = now;
    fixed[0] = true;
    reels[0] = isWinning ? WIN_NUMBER : random(0, MAX_DIGIT + 1);
    stopCount++;
    anyStopped = true;
  }
  if (inputs.stop2Pressed && !fixed[1] && (now - lastStop2Ms >= DEBOUNCE_MS)) {
    lastStop2Ms = now;
    fixed[1] = true;
    reels[1] = isWinning ? WIN_NUMBER : random(0, MAX_DIGIT + 1);
    stopCount++;
    anyStopped = true;
  }
  if (inputs.stop3Pressed && !fixed[2] && (now - lastStop3Ms >= DEBOUNCE_MS)) {
    lastStop3Ms = now;
    fixed[2] = true;
    reels[2] = isWinning ? WIN_NUMBER : random(0, MAX_DIGIT + 1);
    stopCount++;
    anyStopped = true;
  }
  return anyStopped;
}

void updateReelsAndDisplay(unsigned long now) {
  if (now - lastReelMs < REEL_UPDATE_MS) {
    return;
  }
  lastReelMs = now;
  for (int i = 0; i < REEL_COUNT; i++) {
    if (!fixed[i]) {
      reels[i] = random(0, MAX_DIGIT + 1);
    }
  }
  setDisplayInt(reels[0] * 100 + reels[1] * 10 + reels[2]);
}

void updateEffects(unsigned long now) {
  if (currentState == STATE_SPINNING || currentState == STATE_STOPPING) {
    if (now - lastLedMs >= LED_CHASE_MS) {
      lastLedMs = now;
      digitalWrite(PIN_LED_1 + ledIndex, LOW);
      ledIndex = (ledIndex + 1) % 3;
      digitalWrite(PIN_LED_1 + ledIndex, HIGH);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_LED_1 + i, LOW);
    }
  }
}

void runResultLighting(unsigned long now) {
  if (isWinning) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_LED_1 + i, HIGH);
    }
  } else {
    digitalWrite(PIN_LED_1, HIGH);
  }
  resultEndMs = now + RESULT_HOLD_MS;
}
