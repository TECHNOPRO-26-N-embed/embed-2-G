#include <Arduino.h>
#include <stdint.h>

// ===== ピン定義 =====
#define PIN_595_DATA  8
#define PIN_595_LATCH 9
#define PIN_595_CLK   10

#define PIN_DIGIT_1 2
#define PIN_DIGIT_2 3
#define PIN_DIGIT_3 4
#define PIN_DIGIT_4 5

#define PIN_BTN_1 6
#define PIN_BTN_2 7

#define PIN_BUZZER   11
#define PIN_LED_BLUE 12

#define PIN_JOY_X A0
#define PIN_JOY_Y A1

// ===== 状態 =====
#define STATE_SETTING 0
#define STATE_RUN     1
#define STATE_STOP    2
#define STATE_NOTIFY  3

uint8_t currentState = STATE_SETTING;
uint8_t notifyMode = 0;

// ===== タイマー =====
unsigned long lastBtn1Ms = 0;
unsigned long lastBtn2Ms = 0;
unsigned long lastTickMs = 0;
unsigned long lastMuxMs = 0;
unsigned long lastBlinkMs = 0;

// ===== 長押し =====
unsigned long btn1PressStart = 0;
uint8_t btn1Holding = 0;
uint8_t longPressTriggered = 0;

// ===== 入力 =====
int joyXRaw = 512;
int joyYRaw = 512;

uint8_t btn1Prev = HIGH;
uint8_t btn2Prev = HIGH;
uint8_t btn1Edge = 0;
uint8_t btn2Edge = 0;

// ===== ジョイスティック制御 =====
uint8_t joyXNeutral = 1;
uint8_t joyYNeutral = 1;

// ===== 時間 =====
uint8_t setMinutes = 0;
uint8_t setSeconds = 0;
uint16_t remainSeconds = 0;
uint8_t selectedDigit = 0;

// ===== フラグ =====
uint8_t ledState = 0;

// ===== 定数 =====
const unsigned long DEBOUNCE_MS = 50;
const unsigned long COUNTDOWN_MS = 1000;
const unsigned long DISPLAY_MUX_MS = 2;
const unsigned long NOTIFY_BLINK_MS = 250;
const unsigned long LONG_PRESS_MS = 1000;

// ★超重要：安定する閾値
const int JOY_LEFT  = 400;
const int JOY_RIGHT = 620;
const int JOY_CENTER_LOW  = 460;
const int JOY_CENTER_HIGH = 580;

// ===== 7セグ =====
uint8_t segData[10] = {
  0b00111111,0b00000110,0b01011011,0b01001111,
  0b01100110,0b01101101,0b01111101,0b00000111,
  0b01111111,0b01101111
};

int digits[4] = {0,0,0,0};
uint8_t currentDigit = 0;

// ===== プロトタイプ =====
void readInputs(void);
void checkLongPress(void);
void updateStateMachine(unsigned long now);
void updateCountdown(unsigned long now);
void runNotifyOutput(unsigned long now);
void updateDisplay(unsigned long now);
void handleJoystickSetting(void);

// ===== setup =====
void setup(void)
{
  pinMode(PIN_595_DATA, OUTPUT);
  pinMode(PIN_595_LATCH, OUTPUT);
  pinMode(PIN_595_CLK, OUTPUT);

  pinMode(PIN_DIGIT_1, OUTPUT);
  pinMode(PIN_DIGIT_2, OUTPUT);
  pinMode(PIN_DIGIT_3, OUTPUT);
  pinMode(PIN_DIGIT_4, OUTPUT);

  pinMode(PIN_BTN_1, INPUT_PULLUP);
  pinMode(PIN_BTN_2, INPUT_PULLUP);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);

  btn1Prev = digitalRead(PIN_BTN_1);
  btn2Prev = digitalRead(PIN_BTN_2);
}

// ===== loop =====
void loop(void)
{
  unsigned long now = millis();

  readInputs();
  checkLongPress();

  updateStateMachine(now);
  updateCountdown(now);
  runNotifyOutput(now);
  updateDisplay(now);

  if (currentState == STATE_SETTING) {
    handleJoystickSetting();
  }
}

// ===== 入力 =====
void readInputs(void)
{
  uint8_t btn1 = digitalRead(PIN_BTN_1);
  uint8_t btn2 = digitalRead(PIN_BTN_2);
  unsigned long now = millis();

  btn1Edge = 0;
  btn2Edge = 0;

  if (btn1Prev == HIGH && btn1 == LOW && (now - lastBtn1Ms >= DEBOUNCE_MS)) {
    btn1Edge = 1;
    lastBtn1Ms = now;

    btn1PressStart = now;
    btn1Holding = 1;
    longPressTriggered = 0;
  }

  if (btn2Prev == HIGH && btn2 == LOW && (now - lastBtn2Ms >= DEBOUNCE_MS)) {
    btn2Edge = 1;
    lastBtn2Ms = now;
  }

  if (btn1 == HIGH) {
    btn1Holding = 0;
  }

  btn1Prev = btn1;
  btn2Prev = btn2;

  joyXRaw = analogRead(PIN_JOY_X);
  joyYRaw = analogRead(PIN_JOY_Y);
}

// ===== 長押し =====
void checkLongPress(void)
{
  unsigned long now = millis();

  if (btn1Holding && !longPressTriggered) {
    if (now - btn1PressStart >= LONG_PRESS_MS) {

      setMinutes = 0;
      setSeconds = 0;
      remainSeconds = 0;
      currentState = STATE_SETTING;

      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_LED_BLUE, LOW);

      longPressTriggered = 1;
    }
  }
}

// ===== 状態 =====
void updateStateMachine(unsigned long now)
{
  if (btn2Edge) notifyMode = 1 - notifyMode;

  if (longPressTriggered) return;

  switch (currentState) {

    case STATE_SETTING:
      if (btn1Edge && remainSeconds > 0) {
        currentState = STATE_RUN;
        lastTickMs = now;
      }
      break;

    case STATE_RUN:
      if (btn1Edge) currentState = STATE_STOP;
      if (remainSeconds == 0) currentState = STATE_NOTIFY;
      break;

    case STATE_STOP:
      if (btn1Edge) {
        currentState = STATE_RUN;
        lastTickMs = now;
      }
      break;

    case STATE_NOTIFY:
      if (btn1Edge) {
        currentState = STATE_SETTING;
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_LED_BLUE, LOW);
      }
      break;
  }
}

// ===== カウント =====
void updateCountdown(unsigned long now)
{
  if (currentState != STATE_RUN) return;

  if (now - lastTickMs >= COUNTDOWN_MS) {
    lastTickMs = now;
    if (remainSeconds > 0) remainSeconds--;
  }
}

// ===== 表示 =====
void updateDisplay(unsigned long now)
{
  if (now - lastMuxMs < DISPLAY_MUX_MS) return;
  lastMuxMs = now;

  uint16_t sec = remainSeconds;

  digits[0] = (sec / 60) / 10;
  digits[1] = (sec / 60) % 10;
  digits[2] = (sec % 60) / 10;
  digits[3] = (sec % 60) % 10;

  showDigit(currentDigit, digits[currentDigit]);
  currentDigit = (currentDigit + 1) % 4;
}

// ===== 7セグ =====
void showDigit(uint8_t pos, uint8_t num)
{
  digitalWrite(PIN_DIGIT_1, HIGH);
  digitalWrite(PIN_DIGIT_2, HIGH);
  digitalWrite(PIN_DIGIT_3, HIGH);
  digitalWrite(PIN_DIGIT_4, HIGH);

  digitalWrite(PIN_595_LATCH, LOW);
  shiftOut(PIN_595_DATA, PIN_595_CLK, MSBFIRST, segData[num]);
  digitalWrite(PIN_595_LATCH, HIGH);

  if (pos == 0) digitalWrite(PIN_DIGIT_1, LOW);
  if (pos == 1) digitalWrite(PIN_DIGIT_2, LOW);
  if (pos == 2) digitalWrite(PIN_DIGIT_3, LOW);
  if (pos == 3) digitalWrite(PIN_DIGIT_4, LOW);
}

// ===== 通知 =====
void runNotifyOutput(unsigned long now)
{
  if (currentState != STATE_NOTIFY) {
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED_BLUE, LOW);
    return;
  }

  digitalWrite(PIN_BUZZER, HIGH);
}

// ===== ジョイスティック =====
void handleJoystickSetting(void)
{
  // ★中央判定（安定化）
  if (joyXRaw > JOY_CENTER_LOW && joyXRaw < JOY_CENTER_HIGH) {
    joyXNeutral = 1;
  }

  if (joyYRaw > JOY_CENTER_LOW && joyYRaw < JOY_CENTER_HIGH) {
    joyYNeutral = 1;
  }

  // ★桁移動
  if (joyXNeutral) {
    if (joyXRaw >= JOY_RIGHT) {
      selectedDigit = (selectedDigit + 1) % 4;
      joyXNeutral = 0;
    }
    else if (joyXRaw <= JOY_LEFT) {
      selectedDigit = (selectedDigit + 3) % 4;
      joyXNeutral = 0;
    }
  }

  // ★値変更
  if (joyYNeutral) {
    if (joyYRaw >= JOY_RIGHT) {
      setSeconds++;
      joyYNeutral = 0;
    }
    else if (joyYRaw <= JOY_LEFT) {
      if (setSeconds > 0) setSeconds--;
      joyYNeutral = 0;
    }
  }

  if (setSeconds > 59) setSeconds = 59;

  remainSeconds = setMinutes * 60 + setSeconds;
}