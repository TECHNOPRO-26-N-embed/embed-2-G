# 상세 설계서 — 임베디드 개발 실습

<!-- 작성자: 당신의 이름 / 날짜: YYYY-MM-DD / 그룹: ○-○ -->
<!-- 작성자: 고 명군 / 날짜: 2026-05-25 / 그룹: 2-g -->

> **이 문서의 목적**
> 기본 설계서(basic_design.md)에서 “어떤 구조로 만들 것인가”를 결정했습니다.
> 이 상세 설계서에서는 “각 처리를 구체적으로 어떻게 구현할 것인가”를 결정합니다.
> 작성을 마쳤을 때, **코드의 골격이 거의 완성된 상태**를 목표로 합니다.

> [!NOTE]
> **V자 모델에서의 위치**
> 상세 설계서 ↔ **단위 테스트**(함수·부품별 테스트)가 대응됩니다.
> “이 함수가 정상 동작하는가”의 확인은 Section 5의 단위 테스트 명세서에서 계획합니다.
> ※ 필수 기능 전체가 정상 동작하는 “통합 테스트”는 기본 설계서(Section 6)에 기재합니다.

---

# 0. 기본 설계서와의 연결 확인

| 항목 | basic_design.md에서 옮겨 적기 |
|:--|:--|
| 작품 제목 | 인체감지 센서 선풍기 |
| 상태 종류(1-2 상태 전이에서) | 대기 중 · 동작 중 · 정지 중 |
| 구현할 함수 개수(2-2 함수 목록에서) | 11개 |
| 전역 변수 총 바이트 수(2-1 SRAM 확인에서) | 29B |

---

# 1. 전역 변수·상수 설계

> ※ 기본 설계서(2-1 데이터 설계)를 바탕으로, **자료형과 초기값까지** 결정합니다.
> 여기서 설계한 변수는 이후 함수 설계에서 그대로 사용합니다.

```cpp
【핀 정의】(basic_design.md 3-1에서 복사)
  PIR_PIN          = 7     // PIR 인체감지 센서
  MOTOR_IN1_PIN    = 3     // L293D 모터 드라이버 IC
  MOTOR_IN2_PIN    = 4     // L293D 모터 드라이버 IC
  MOTOR_PWM_PIN    = 5     // L293D 모터 드라이버 IC(PWM 제어)
  SEG_DATA_PIN     = 8     // 4자리 7세그먼트 디스플레이
  SEG_CLOCK_PIN    = 9     // 4자리 7세그먼트 디스플레이
  SEG_LATCH_PIN    = 10    // 4자리 7세그먼트 디스플레이
  LED_PIN          = 13    // LED

【상태 관리】(basic_design.md 1-2의 상태명에서 복사)
  currentState : int = 0   // 0:대기 중 1:동작 중 2:정지 중

【타이머(millis()용)】(basic_design.md 2-3에서 복사)
  lastDetectMillis : unsigned long = 0
  lastPwmMillis    : unsigned long = 0

【센서·입력값】(basic_design.md 2-1에서 복사)
  pirValue : int = 0       // PIR 센서 감지값

【PWM·표시 관리】
  pwmValue     : int = 0   // 모터에 출력할 PWM 값
  displayValue : int = 0   // 7세그에 표시할 값

【기타 플래그·카운터】
  motorRunning : bool = false   // 모터 동작 여부

【상수】
  stopTime : const unsigned long = 20000   // 20초
  maxPwm   : const int = 100               // PWM 최대값
```

---

# 2. 각 함수의 상세 설계

> ※ 기본 설계서(2-2 함수 목록)에서 정의한 각 함수의 “내용”을 설계합니다.
> **의사코드**(일본어 + 처리 흐름) 형식으로 작성하세요.
> 실제 C++ 코드는 작성하지 않아도 됩니다.

---

## `setup()` — 초기화 처리

```text
【처리 흐름】
1. 핀 모드를 설정한다
   - PIR 인체감지 센서 D7 → INPUT
   - L293D 모터 드라이버 IC D3, D4, D5 → OUTPUT
   - 4자리 7세그먼트 디스플레이 D8, D9, D10 → OUTPUT
   - LED D13 → OUTPUT

2. 초기 상태를 설정한다
   - currentState 를 “대기 중”으로 설정
   - pwmValue 를 0으로 설정
   - motorRunning 을 false 로 설정
   - lastDetectMillis 를 0으로 설정
   - lastPwmMillis 를 0으로 설정

3. 출력을 초기화한다
   - 모터를 정지시킨다
   - LED를 끈다
   - 7세그먼트 디스플레이에 “OFF” 표시

4. Serial.begin(9600)을 실행한다
   - 디버깅용으로 상태와 센서값을 확인할 수 있도록 한다
```

---

## `loop()` — 메인 루프

```text
【처리 흐름】

＜매 루프마다 실행할 것＞
  - 현재 시간을 가져온다: currentMillis = millis()
  - PIR 인체감지 센서 값을 읽는다
  - LED와 7세그먼트 디스플레이 표시를 갱신한다

＜currentState 가 0(대기 중)일 때＞
  - PIR 센서가 사람을 감지할 때까지 대기
  - 사람을 감지한 경우:
      - lastDetectMillis 를 현재 시간으로 갱신
      - currentState 를 1(동작 중)으로 변경

＜currentState 가 1(동작 중)일 때＞
  - 모터를 PWM 제어로 회전시킨다
  - PWM 값을 점차 증가시킨다
  - 7세그먼트 디스플레이에 PWM 값을 표시한다
  - 사람을 감지한 경우:
      - lastDetectMillis 갱신
  - 20초 동안 사람을 감지하지 못한 경우:
      - currentState 를 2(정지 중)으로 변경

＜currentState 가 2(정지 중)일 때＞
  - 모터를 정지시킨다
  - 7세그먼트 디스플레이에 “OFF” 표시
  - PIR 센서가 다시 사람을 감지한 경우:
      - lastDetectMillis 갱신
      - currentState 를 1(동작 중)으로 변경
```

---

## `readPirSensor()` — PIR 센서 감지 상태 읽기

**basic_design.md 2-2와의 대응:** PIR 센서 감지 상태 읽기

**인수:** 없음

**반환값:** bool : 사람 감지 여부

```text
【처리 흐름】
1. PIR 센서 입력값을 읽는다
2. HIGH이면 true 반환
3. LOW이면 false 반환

【오류·이상 처리】
- 센서값이 불안정한 경우:
  바로 상태를 변경하지 않고 다음 루프에서 다시 확인한다
```

---

## `updateState()` — 상태 전환

**basic_design.md 2-2와의 대응:** 센서값과 타이머를 기반으로 상태 변경

**인수:** `detected`(bool) : 사람 감지 여부

**반환값:** void

```text
【처리 흐름】
1. detected 가 true이면 lastDetectMillis 갱신
2. 대기 중에서 detected 가 true이면 동작 중으로 변경
3. 동작 중에서 20초 동안 감지되지 않으면 정지 중으로 변경

【오류·이상 처리】
- 상태 번호가 예상 외 값인 경우:
  currentState 를 0(대기 중)으로 되돌린다
```

---

## `updateDisplay()` — 표시 갱신

**basic_design.md 2-2와의 대응:** 상태에 따라 PWM 값 또는 OFF를 7세그에 표시

**인수:** `pwmValue`(int) : 표시할 PWM 값

**반환값:** void

```text
【처리 흐름】
1. currentState 확인
2. 동작 중이면 showPwmValue() 호출
3. 대기 중 또는 정지 중이면 showOff() 호출

【오류·이상 처리】
- pwmValue 가 0 미만 또는 100 초과인 경우:
  0~100 범위로 보정하여 표시
```

---

## `startFan()` — 모터 동작

**basic_design.md 2-2와의 대응:** 사람을 감지했을 때 모터 동작

**인수:** `pwmValue`(int) : 모터에 출력할 PWM 값

**반환값:** void

```text
【처리 흐름】
1. 모터 드라이버 제어핀을 회전 방향으로 설정
2. PWM 값을 모터에 출력
3. motorRunning 을 true 로 설정

【오류·이상 처리】
- pwmValue 가 비정상 값인 경우:
  0~100 범위로 보정 후 출력
```

---

## `stopFan()` — 모터 정지

**basic_design.md 2-2와의 대응:** 20초 동안 감지가 없을 경우 모터 정지

**인수:** 없음

**반환값:** void

```text
【처리 흐름】
1. 모터 PWM 출력을 0으로 설정
2. 모터 드라이버 제어핀을 정지 상태로 설정
3. motorRunning 을 false 로 설정

【오류·이상 처리】
- 모터 정지 후에도 동작 상태가 남아있는 경우:
  pwmValue 를 0으로 되돌리고 상태를 정지 중으로 설정
```

---

## `showPwmValue()` — PWM 값 표시

**basic_design.md 2-2와의 대응:** 4자리 7세그먼트 디스플레이에 PWM 값을 0000~0100 형태로 표시

**인수:** `pwmValue`(int) : 표시할 PWM 값

**반환값:** void

```text
【처리 흐름】
1. pwmValue 를 4자리 표시용 값으로 변환
2. 7세그먼트 디스플레이에 데이터 전송
3. 표시 갱신

【오류·이상 처리】
- pwmValue 가 0 미만 또는 100 초과인 경우:
  표시 가능한 범위로 보정
```

---

## `showOff()` — OFF 표시

**basic_design.md 2-2와의 대응:** 모터 정지 시 7세그먼트 디스플레이에 OFF 표시

**인수:** 없음

**반환값:** void

```text
【처리 흐름】
1. 표시 내용을 “OFF”로 설정
2. 7세그먼트 디스플레이에 데이터 전송
3. 표시 갱신

【오류·이상 처리】
- OFF 표시가 불가능한 경우:
  모든 자릿수를 소등
```

---

## `checkNoMotionTimeout()` — 미감지 시간 확인

**basic_design.md 2-2와의 대응:** 마지막 감지 이후 20초 경과 여부 확인

**인수:** `lastDetectMillis`(unsigned long) : 마지막으로 사람을 감지한 시각

**반환값:** bool : 20초 경과 여부

```text
【처리 흐름】
1. millis() 로 현재 시간 획득
2. 현재 시간과 lastDetectMillis 차이 계산
3. 20초 이상 경과 시 true 반환

【오류·이상 처리】
- lastDetectMillis 가 미설정 상태인 경우:
  false 반환 후 즉시 정지하지 않음
```

---

## `updateLed()` — LED 상태 갱신

**basic_design.md 2-2와의 대응:** 동작 중에는 LED 점등, 정지 중에는 소등

**인수:** `state`(int) : 현재 상태

**반환값:** void

```text
【처리 흐름】
1. state 확인
2. 동작 중이면 LED 점등
3. 대기 중 또는 정지 중이면 LED 소등

【오류·이상 처리】
- state 가 예상 외 값인 경우:
  LED 소등
```


## 3. 중요 로직 상세 설계

### 3-1. 채터링 방지(디바운스 처리)

> ※ 버튼을 사용하는 경우 반드시 설계하세요.

```text
【개념】
버튼이 눌렸을 때, 50ms 이내의 연속 입력은 같은 1회의 입력으로 간주하고 무시한다.

【처리 흐름】
1. 버튼 디지털값 읽기(digitalRead)
2. 이전 확정 시각(lastDebounceTime)과의 경과 시간 계산
3. 경과 시간 < DEBOUNCE_DELAY(예: 50ms) → 무시
4. 경과 시간 ≥ DEBOUNCE_DELAY → 버튼 상태 확정
5. lastDebounceTime 갱신

【필요 변수(Section 1에 추가되어 있는지 확인)】
lastDebounceTime : unsigned long   // 이전 확정 시각
DEBOUNCE_DELAY : const int = 50   // 채터링 판정 시간(ms)
```

---

### 3-2. millis() 를 이용한 타이머 관리

```text
【개념】
“이전에 실행한 시각”을 기록해 두고,
“현재 시각 − 이전 시각 ≥ 주기”이면 실행한다.

【처리 흐름(예: LED 점멸)】
1. now = millis()
2. now - lastMillis_LED >= LED_INTERVAL 인지 확인
3. 조건 충족 시:
   - LED ON/OFF 전환
   - lastMillis_LED = now
4. 조건 미충족 시:
   - 아무것도 하지 않고 다음 루프에서 재확인

【자신의 시스템에서 millis() 를 사용하는 처리】
(basic_design.md 2-3의 타이밍 설계 내용을 복사하여 구체화)
```

---

### 3-3. 기타 중요 로직(선택)

【로직 이름】
PIR 센서 오감지 대책과 20초 정지 판정

【개념】
PIR 센서는 사람의 움직임이나 주변 환경에 따라 일시적으로 HIGH/LOW 가 변할 수 있다.
따라서 LOW가 1회 발생했다고 바로 선풍기를 정지시키지 않고,
마지막 감지 시각으로부터 20초가 경과했을 때 정지한다.

【처리 흐름】
1. PIR 센서값 읽기
2. 사람 감지 시 lastDetectMillis 를 현재 시각으로 갱신
3. 감지 중에는 모터 동작 및 PWM 값 표시
4. 감지되지 않는 상태가 지속되면 현재 시각과 lastDetectMillis 차이 확인
5. 20초 이상 경과 시:
   - 모터 정지
   - 7세그먼트 디스플레이에 “OFF” 표시

【입력값과 출력값 관계】

PIR 센서 = HIGH
→ 사람 감지
→ 모터 동작
→ LED 점등
→ 7세그먼트 디스플레이에 PWM 값 표시

PIR 센서 = LOW + 20초 미만
→ 즉시 정지하지 않음
→ 모터 계속 동작
→ PWM 표시 유지

PIR 센서 = LOW + 20초 이상
→ 사람 없음으로 판단
→ 모터 정지
→ LED 소등
→ “OFF” 표시
```

---

## 4. 디버그 출력 계획(선택)

> **【선택】** 함수 설계(Section 2)와 병행해서 작성하면 효과적입니다.
> “동작하지 않을 때” 무엇을 확인해야 하는지 미리 계획합니다.
> 구현 후에는 불필요한 Serial.println() 을 삭제할 것.

| No | 확인 내용 | 삽입 함수 | Serial.println 예시 |
|:---|:---|:---|:---|
| 1 | PIR 센서값 정상 취득 여부 | `readPirSensor()` | `Serial.println(pirValue);` |
| 2 | 상태 전이 정상 여부 | `loop()` | `Serial.println(currentState);` |
| 3 | 감지 시각 갱신 여부 | `updateState()` | `Serial.println(lastDetectMillis);` |
| 4 | PWM 값 변경 여부 | `startFan()` | `Serial.println(pwmValue);` |
| 5 | 20초 미감지 판정 여부 | `checkNoMotionTimeout()` | `Serial.println("No motion timeout");` |
| 6 | 모터 정지 처리 여부 | `stopFan()` | `Serial.println("Fan stopped");` |
| 7 | OFF 표시 호출 여부 | `showOff()` | `Serial.println("Display OFF");` |
| 8 | LED 상태 변경 여부 | `updateLed()` | `Serial.println("LED Updated");` |

---

## 5. 단위 테스트 명세서(V자 모델: 상세 설계 ↔ 단위 테스트)

> ※ 각 함수·부품이 “단독으로 정상 동작하는지” 확인하기 위한 테스트 항목을 설계합니다.
> “실제 결과”란은 구현 후 작성합니다.

---

## 5-1. 입력계 테스트

| No | 테스트 대상 함수 | 입력·조작 | 기대 결과 | 실제 결과 | 합격 여부 |
|:---|:---|:---|:---|:---|:---|
| 1 | `readPirSensor()` | PIR 센서 앞에서 움직임 | true 반환 | | [ ] |
| 2 | `readPirSensor()` | PIR 센서 앞에 사람이 없는 상태 | false 반환 | | [ ] |
| 3 | `readPirSensor()` | 센서 주변에서 짧게 움직임 | 오감지 시에도 바로 상태가 변경되지 않음 | | [ ] |
| 4 | `checkNoMotionTimeout()` | 마지막 감지 후 20초 미만 | false 반환 | | [ ] |
| 5 | `checkNoMotionTimeout()` | 마지막 감지 후 20초 이상 경과 | true 반환 | | [ ] |
| 6 | `updateState()` | detected=true, currentState=0(대기 중) 전달 | currentState 가 1(동작 중)로 변경 | | [ ] |
| 7 | `updateState()` | detected=false, 20초 이상 미감지 | currentState 가 2(정지 중)로 변경 | | [ ] |

---

## 5-2. 출력계 테스트

| No | 테스트 대상 함수 | 입력·조작 | 기대 결과 | 실제 결과 | 합격 여부 |
|:---|:---|:---|:---|:---|:---|
| 1 | `startFan()` | pwmValue 에 50 전달 | 모터 회전 | | [ ] |
| 2 | `stopFan()` | 모터 동작 중 호출 | 모터 정지 | | [ ] |
| 3 | `showPwmValue()` | pwmValue 에 50 전달 | 7세그먼트 디스플레이에 0050 표시 | | [ ] |
| 4 | `showPwmValue()` | pwmValue 에 100 전달 | 7세그먼트 디스플레이에 0100 표시 | | [ ] |
| 5 | `showOff()` | 모터 정지 상태에서 호출 | 7세그먼트 디스플레이에 OFF 표시 | | [ ] |
| 6 | `updateLed()` | state=1(동작 중) 전달 | LED 점등 | | [ ] |
| 7 | `updateLed()` | state=0(대기 중) 또는 state=2(정지 중) 전달 | LED 소등 | | [ ] |
| 8 | `updateDisplay()` | currentState=1, pwmValue=50 | `showPwmValue()` 호출 및 0050 표시 | | [ ] |
| 9 | `updateDisplay()` | currentState=0 또는 2 | `showOff()` 호출 및 OFF 표시 | | [ ] |

---

## 5-3. 타이밍·병행 동작 테스트

| No | 테스트 내용 | 테스트 절차 | 기대 결과 | 실제 결과 | 합격 여부 |
|:---|:---|:---|:---|:---|:---|
| 1 | delay() 에 의한 처리 정지 여부 | 모터 동작 중 PIR 센서 앞에서 움직임 | 센서 입력이 무시되지 않음 | | [ ] |
| 2 | millis() 타이머 주기 정확도 | PWM 값 증가 간격을 Serial.println 으로 확인 | 설계한 주기대로 PWM 값 증가 | | [ ] |
| 3 | 20초 정지 판정 | 사람 감지 후 자리를 떠나 20초 대기 | 20초 후 모터 정지 | | [ ] |
| 4 | 표시 갱신과 센서 감시의 병행 동작 | PWM 값 표시 중 다시 사람 감지 | 표시 갱신 중에도 감지 시각 갱신 | | [ ] |
| 5 | 정지 후 재시작 | 정지 중 PIR 센서 앞에서 움직임 | currentState 가 1(동작 중)이 되고 모터 재회전 | | [ ] |

---

## 6. AI 리뷰 기록

> 그룹 리뷰 전에 반드시 실시할 것.

### Q1: 구현상의 문제 확인

> “이 상세 설계서에 작성한 함수와 처리 흐름을 바탕으로 Arduino 코드를 작성합니다. 버그가 발생하기 쉬운 부분·빠진 처리·자료형 문제는 있습니까?”

**AI 답변(요약):**

기본적으로 구현 가능한 내용이지만, 몇 가지 버그가 발생하기 쉬운 부분이 있습니다.

우선 `pwmValue` 는 0~100 으로 설계되어 있지만,
Arduino 의 `analogWrite()` 는 0~255 값을 사용하므로 구현 시 변환 처리가 필요합니다.

또한 `millis()` 를 사용하는 시간 계산에서는 `int` 가 아니라 `unsigned long` 을 사용해야 합니다.
특히 20초 판정에서는
`millis() - lastDetectMillis >= stopTime`
형태로 계산하면 안전합니다.

다음으로 `lastDetectMillis` 초기값이 0인 상태라면,
기동 직후 20초 경과로 판단될 가능성이 있습니다.
따라서 사람 감지 시 반드시 현재 시각으로 갱신해야 합니다.

또한 상태 번호가 예상 외 값이 되었을 경우 처리도 필요합니다.
상세 설계서에서는 이상 시 대기 상태로 복귀한다고 작성되어 있으므로,
구현 시에도 `currentState = 0` 으로 되돌리는 처리를 넣는 것이 좋습니다.

마지막으로 `delay()` 를 사용하면 센서 확인이나 표시 갱신이 멈출 가능성이 있으므로,
PWM 증가 및 20초 판정은 `millis()` 를 사용해 처리해야 합니다.

위 사항들을 주의하면 큰 문제 없이 Arduino 에서 구현 가능하다고 판단됩니다.

**대응한 내용：**

---

### Q2: 단위 테스트 명세 확인

> “Section 5 의 단위 테스트 명세서에서 각 함수 동작이 올바르게 검증되고 있습니까? 부족한 테스트 항목이나 경계값 테스트가 필요한 부분을 알려주세요.”

**AI 답변(요약):**

현재 단위 테스트 명세는 기본적인 동작 확인으로는 충분합니다.
다만 더 정확한 검증을 위해 몇 가지 항목을 추가하면 좋습니다.

특히 pwmValue 의 0 과 100,
미감지 시간이 정확히 20초인 경우,
예상 외 상태값 등은 경계값으로 확인하는 것이 좋습니다.

모든 이상계를 완전히 망라할 필요는 없지만,
대표적인 경계값을 조금 추가하면 단위 테스트 명세로서 더 명확해집니다.

---

**대응한 내용：**

### 추가 테스트 항목

| No | 테스트 대상 | 입력·조작 | 기대 결과 |
|:---|:---|:---|:---|
| 8 | `checkNoMotionTimeout()` | 정확히 20초 경과 | true 반환 |
| 9 | `updateState()` | 예상 외 state 설정 | 대기 상태로 복귀 |
| 10 | `startFan()` | pwmValue = 0 | 모터 정지 |
| 11 | `startFan()` | pwmValue = 100 | 최대 출력으로 회전 |
| 12 | `showPwmValue()` | pwmValue = 0 | 0000 표시 |
| 13 | `updateLed()` | 예상 외 state | LED 소등 |
| 14 | 정지 판정 | 19초 경과 시 확인 | 모터 정지하지 않음 |

---

## 7. 그룹 리뷰 기록

### 7-1. 지적 사항 목록

| No | 지적 내용 | 지적자 | 대응 |
|:---|:---|:---|:---|
| 1 |  |  |  |
| 2 |  |  |  |
| 3 |  |  |  |

---

### 7-2. 리뷰 후 수정한 점

-
-

---

*초판: YYYY-MM-DD / AI 리뷰: YYYY-MM-DD / 그룹 리뷰 후 수정: YYYY-MM-DD*
