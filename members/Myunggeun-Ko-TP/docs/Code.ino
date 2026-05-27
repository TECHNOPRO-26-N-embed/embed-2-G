// 74HC595 pins
int latch = 9;
int clock = 10;
int data  = 8;

// 4-digit segment pins
int digitPins[4] = {2, 3, 4, 5};

// PIR sensor
int ledPin = 13;
int pirPin = 12;
int pirValue;

// PIR state
int lastPirState = LOW;

// Motor pins
#define ENABLE 11
#define DIRA 6
#define DIRB 7

byte table[] = {
  0x3f, // 0
  0x06, // 1
  0x5b, // 2
  0x4f, // 3
  0x66, // 4
  0x6d, // 5
  0x7d, // 6
  0x07, // 7
  0x7f, // 8
  0x6f  // 9
};

// OFF 문자
byte SEG_O = 0x3f;
byte SEG_F = 0x71;

int pwmValue = 0;
bool manualPowerMode = false;

unsigned long lastMotorTime = 0;
unsigned long motorInterval = 10;

unsigned long lastDetectTime = 0;

unsigned long lastPrintTime = 0;
unsigned long printInterval = 200;

// 20초 유지
unsigned long keepOnTime = 20000;

// 정지 직후 재기동 방지 시간
unsigned long restartLockTime = 5000;
unsigned long stopTime = 0;

// 상태값
// 0 = 대기중
// 1 = 동작중
// 2 = 정지중
int currentState = 0;

void setup() {

  pinMode(latch, OUTPUT);
  pinMode(clock, OUTPUT);
  pinMode(data, OUTPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(digitPins[i], OUTPUT);
    digitalWrite(digitPins[i], HIGH);
  }

  pinMode(ledPin, OUTPUT);
  pinMode(pirPin, INPUT);

  pinMode(ENABLE, OUTPUT);
  pinMode(DIRA, OUTPUT);
  pinMode(DIRB, OUTPUT);

  digitalWrite(DIRA, HIGH);
  digitalWrite(DIRB, LOW);

  stopFan();

  Serial.begin(9600);

  Serial.println("System Start");
}

// PIR 센서 값을 읽어서 감지되면 true 반환
bool readPirSensor() {

  pirValue = digitalRead(pirPin);

  if (pirValue == HIGH) {
    return true;
  } else {
    return false;
  }
}

// 마지막 감지 후 20초가 지났는지 확인
bool checkNoMotionTimeout() {

  if (millis() - lastDetectTime >= keepOnTime) {
    return true;
  } else {
    return false;
  }
}

// PIR 감지 여부에 따라 상태 변경
void updateState(bool detected) {

  // 정지중일 때는 정지 직후 일정 시간 동안 PIR 무시
  if (currentState == 2) {

    if (millis() - stopTime < restartLockTime) {
      lastPirState = pirValue;
      return;
    }

    // 재기동 방지 시간이 지난 뒤,
    // PIR이 LOW에서 HIGH로 새로 바뀔 때만 다시 켜짐
    if (detected == true && lastPirState == LOW) {

      currentState = 1;
      manualPowerMode = false;
      pwmValue = 1;
      lastDetectTime = millis();

      Serial.println("Motion Detected / Motor ON");
    }

    lastPirState = pirValue;
    return;
  }

  // PIR 감지
  if (detected == true && lastPirState == LOW) {

    lastDetectTime = millis();

    if (currentState != 1) {
      currentState = 1;
      manualPowerMode = false;
      pwmValue = 1;
    }

    Serial.println("Motion Detected");
  }

  // 20초 동안 감지 없으면 정지
  if (currentState == 1 && checkNoMotionTimeout() == true) {

    currentState = 2;
    stopTime = millis();
    manualPowerMode = false;

    stopFan();

    Serial.println("No Motion / Motor OFF");
  }

  lastPirState = pirValue;
}

// 전달받은 PWM 값으로 모터 회전
void startFan(int value) {

  analogWrite(ENABLE, value);
}

// 모터 정지 및 PWM 값 초기화
void stopFan() {

  analogWrite(ENABLE, 0);

  pwmValue = 0;
}

// 상태에 따라 LED ON/OFF
void updateLed(int state) {

  if (state == 1) {
    digitalWrite(ledPin, HIGH);
  } else {
    digitalWrite(ledPin, LOW);
  }
}

// 74HC595로 세그먼트 데이터 전송
void sendData(byte value) {

  digitalWrite(latch, LOW);

  shiftOut(data, clock, MSBFIRST, value);

  digitalWrite(latch, HIGH);
}

// 모든 세그먼트 OFF
void allDigitsOff() {

  for (int i = 0; i < 4; i++) {
    digitalWrite(digitPins[i], HIGH);
  }
}

// 지정한 위치에 숫자 1개 표시
void showOneDigit(int pos, int num) {

  allDigitsOff();

  sendData(table[num]);

  digitalWrite(digitPins[pos], LOW);

  delay(2);
}

// PWM 값을 4자리 숫자로 표시
void showPwmValue(int value) {

  int d1 = value / 1000;
  int d2 = (value / 100) % 10;
  int d3 = (value / 10) % 10;
  int d4 = value % 10;

  showOneDigit(0, d1);
  showOneDigit(1, d2);
  showOneDigit(2, d3);
  showOneDigit(3, d4);
}

// 세그먼트에 OFF 표시
void showOff() {

  allDigitsOff();
  sendData(0x00);
  digitalWrite(digitPins[0], LOW);
  delay(2);

  allDigitsOff();
  sendData(SEG_O);
  digitalWrite(digitPins[1], LOW);
  delay(2);

  allDigitsOff();
  sendData(SEG_F);
  digitalWrite(digitPins[2], LOW);
  delay(2);

  allDigitsOff();
  sendData(SEG_F);
  digitalWrite(digitPins[3], LOW);
  delay(2);
}

// 현재 상태에 따라 숫자 또는 OFF 표시
void updateDisplay() {

  if (currentState == 1) {
    showPwmValue(pwmValue);
  } else {
    showOff();
  }
}

// 모터 PWM 값을 점점 증가
void updateFanPower() {

  if (manualPowerMode == true) {
    startFan(pwmValue);
    return;
  }

  if (currentState == 1) {

    if (millis() - lastMotorTime >= motorInterval) {

      lastMotorTime = millis();

      if (pwmValue < 255) {
        pwmValue++;
      }

      startFan(pwmValue);

      if (millis() - lastPrintTime >= printInterval) {

        unsigned long currentTime = millis();

        Serial.print("Interval: ");
        Serial.print(currentTime - lastPrintTime);

        Serial.print(" ms / Motor Power: ");
        Serial.println(pwmValue);

        lastPrintTime = currentTime;
      }
    }
  }
}

// 메인 반복 실행
void loop() {

  // 시리얼 명령 처리
  if (Serial.available() > 0) {

    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "ON") {

      manualPowerMode = false;

      lastDetectTime = millis();

      if (currentState != 1) {
        currentState = 1;
        pwmValue = 1;
      }

      Serial.println("Serial Command: ON");
    }

    else if (command == "OFF") {

      currentState = 2;
      stopTime = millis();

      manualPowerMode = false;

      stopFan();

      Serial.println("Serial Command: OFF");
    }

    else {

      int inputPower = command.toInt();

      if (inputPower < 0) {
        inputPower = 0;
      }

      if (inputPower > 255) {
        inputPower = 255;
      }

      currentState = 1;
      manualPowerMode = true;

      pwmValue = inputPower;
      lastDetectTime = millis();

      startFan(pwmValue);

      Serial.print("Manual Power: ");
      Serial.println(pwmValue);
    }
  }

  // PIR 센서 감지
  bool detected = readPirSensor();

  // 상태 업데이트
  updateState(detected);

  // LED 상태 업데이트
  updateLed(currentState);

  // 모터 PWM 업데이트
  updateFanPower();

  // 디스플레이 업데이트
  updateDisplay();
}