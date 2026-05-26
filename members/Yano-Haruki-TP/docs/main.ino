#include <DHT11.h>
#include "SevSeg.h"

SevSeg sevseg;

DHT11 dht11(2);

void setup() {
  byte numDigits = 4;   // ← 4桁に変更
  byte digitPins[] = {3, 4, 5, A0}; // 4桁目追加（接続ピンに合わせて変更）
  byte segmentPins[] = {6, 7, 8, 9, 10, 11, 12, 13};

  bool resistorsOnSegments = 0;

  sevseg.begin(COMMON_ANODE, numDigits, digitPins, segmentPins, resistorsOnSegments);
  sevseg.setBrightness(90);
}

unsigned long lastRead = 0;
bool firstRead = true;
int temperature = 0;

void loop() {
  unsigned long interval = firstRead ? 1000 : 60000;

  if (millis() - lastRead >= interval) {
    lastRead = millis();
    firstRead = false;

    int result = dht11.readTemperature();

    // 読み取り失敗 or センサー未接続
    if (result < 0) {
      sevseg.setNumber(888);
    } else {
      temperature = result;
      sevseg.setNumber(temperature);
    }
  }

  sevseg.refreshDisplay();
}