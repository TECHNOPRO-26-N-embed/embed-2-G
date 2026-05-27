# 詳細設計書 — 組込み開発実習

<!-- 作成者: 佐藤瑶士 / 日付: 2026-05-25 / グループ: 2-G -->

> **このドキュメントの目的**
> 基本設計書（basic_design.md）で「**どのような構造で作るか**」を決めました。
> この詳細設計書では「**各処理を具体的にどう実装するか**」を決めます。
> 書き終わったとき、**コードの骨格がほぼ完成している**状態を目指してください。

> [!NOTE]
> **V字モデルにおける位置づけ**
> 詳細設計書 ←→ **単体テスト**（関数・部品ごとのテスト）が対応します。
> 「この関数が正しく動くか」の確認は Section 5 の単体テスト仕様書で計画します。
> ※ 必須機能全体が動くかの「結合テスト」は基本設計書（Section 6）に記載します。

---

## 0. 基本設計書との接続確認

| 項目 | basic_design.md から転記 |
|:--|:--|
| 作品タイトル | ボタン式及びセンサ式ストップウォッチ |
| 状態の種類（1-2 状態遷移から） | 停止表示(00.00), 計測中, 停止表示(保持), 上限停止 |
| 実装する関数の数（2-2 関数一覧から） | 14個 |
| グローバル変数の合計バイト数（2-1 SRAM確認から） | 約40B |

---

## 1. グローバル変数・定数の設計

> ※ 基本設計書（2-1 データ設計）をもとに、**型と初期値まで**決めます。
> ここで設計した変数は、この後の関数設計でそのまま使います。

```
【ピン定義】（basic_design.md 3-1 から転記）
  PIN_BUTTON = 2          // タクトスイッチ入力ピン（INPUT_PULLUP）
  PIN_ULTRASONIC_TRIG = 3 // 超音波センサTRIG出力ピン（パルス送信）
  PIN_ULTRASONIC_ECHO = 4 // 超音波センサECHO入力ピン（パルス幅受信）
  PIN_7SEG_SER = 11       // 74HC595 SER（シリアルデータ入力）
  PIN_7SEG_RCLK = 12      // 74HC595 RCLK（ラッチクロック）
  PIN_7SEG_SRCLK = 13     // 74HC595 SRCLK（シフトクロック）
  PIN_7SEG_DIGIT1 = 5     // 4桁7セグ桁選択（左端）
  PIN_7SEG_DIGIT2 = 6
  PIN_7SEG_DIGIT3 = 7
  PIN_7SEG_DIGIT4 = 8     // 4桁7セグ桁選択（右端）
  PIN_7SEG_MINUTE = 9     // 1桁7セグ（分表示、任意機能）
  PIN_LED_RED = 10        // 赤LED出力（start/stop通知）

【定数（しきい値・周期・上限値）】
  STATE_STOP_RESET = 0            // 停止表示(00.00)
  STATE_RUNNING = 1               // 計測中
  STATE_STOP_HOLD = 2             // 停止表示(保持)
  STATE_MAX_HOLD = 3              // 上限停止(59.99固定)

  DEBOUNCE_DELAY_MS = 30          // ボタンチャタリング無視時間（ms）：これ以内は連続で押しても１回判定
  LONG_PRESS_MS = 1000            // 長押し判定時間（ms）＝１秒押しっぱ
  SENSOR_INTERVAL_MS = 50         // 超音波測定周期（ms）
  SENSOR_DEADTIME_MS = 300        // start/stop直後の再トリガ無効時間（ms）：startしてstopが押せるようになるまでの時間
  SENSOR_MIN_CM = 3               // センサ有効下限距離（cm）：近値
  SENSOR_MAX_CM = 5               // センサ有効上限距離（cm）：遠値
  SENSOR_HIT_REQUIRED = 2         // 有効化に必要な連続一致回数
  TICK_INTERVAL_MS = 10           // 計時加算周期（ms）= 0.01秒
  MAX_ELAPSED_CS = 5999           // 表示上限（59.99秒 = 5999cs）→１分表示にすることを考慮し、00.00を返させるか検討
  LED_PULSE_MS = 1000             // start/stop通知LED点灯時間（ms）

  EVENT_NONE = 0                  // 入力イベントなし
  EVENT_BUTTON_SHORT = 1          // ボタン短押し
  EVENT_BUTTON_LONG = 2           // ボタン長押し
  EVENT_SENSOR_TRIGGER = 3        // センサ有効検知

【状態管理】
  currentState : uint8_t = STATE_STOP_RESET
    // 現在状態を管理する（状態遷移の唯一の基準）
    // 変更タイミング: updateStateMachine() のみ

  elapsedCs : uint16_t = 0
    // 現在の計測値（単位: centisecond = 1/100秒）
    // 有効範囲: 0..MAX_ELAPSED_CS
    // 更新タイミング: 計測中に updateElapsedTime() が10msごとに加算

  holdCs : uint16_t = 0
    // 停止時に表示保持する計測値
    // 更新タイミング: RUN->STOP遷移時に captureAndHoldTime() で更新

【ボタン・センサ・タイマー】
  buttonStable : bool = false
    // デバウンス後に確定したボタン状態
    // true=押下中, false=非押下

  lastButtonRaw : bool = false
    // 直前ループの生ボタン値（デバウンス前）
    // チャタリング判定で差分比較に使用

  buttonPressStartMs : unsigned long = 0
    // 押下開始時刻（millis）
    // 短押し/長押しの境界判定に使用

  lastButtonChangeMs : unsigned long = 0
    // 生ボタン値が最後に変化した時刻（millis）
    // DEBOUNCE_DELAY_MS経過判定に使用

  distanceCm : uint16_t = 999
    // 最新の距離測定値（cm）
    // 999は初期化直後の無効値として扱う

  sensorConsecutiveHit : uint8_t = 0
    // 閾値内距離の連続一致回数
    // SENSOR_HIT_REQUIRED到達で有効イベント化

  lastSensorReadMs : unsigned long = 0
    // センサを最後に測定した時刻（周期実行用）

  lastSensorTriggerMs : unsigned long = 0
    // センサイベントを最後に採用した時刻
    // SENSOR_DEADTIME_MS以内の再トリガを無効化

  lastTickMs : unsigned long = 0
    // 計時加算の基準時刻
    // now - lastTickMs >= TICK_INTERVAL_MS で elapsedCs を加算

  ledOffAtMs : unsigned long = 0
    // LED消灯予定時刻（millis）
    // now < ledOffAtMs なら点灯

  minuteOptional : bool = false
    // 任意機能（1桁7セグ分表示）の有効/無効フラグ

  isButtonEvent : bool = false
    // 当該ループでボタンイベントが成立したか

  isSensorEvent : bool = false
    // 当該ループでセンサイベントが成立したか

  inputEvent : uint8_t = EVENT_NONE
    // updateStateMachine()へ渡す確定イベント
    // 同時成立時はボタン優先で確定

【その他のフラグ・カウンター】
  lastDisplayMuxMs : unsigned long = 0
    // 7セグ多重化の更新基準時刻

  currentDigitIndex : uint8_t = 0
    // 現在点灯中の桁番号（0..3）

  startupLedDone : bool = false
    // 起動確認LEDを既に実施したか
```

---

## 2. 各関数の詳細設計

> ※ 基本設計書（2-2 関数一覧）で定義した各関数の「中身」を設計します。
> **疑似コード**（日本語＋処理の流れ）で書いてください。実際のC++コードは書かなくてOKです。

---

### `setup()` — 初期化処理

```
【処理の流れ】
1. ピンモードを設定する
   - PIN_BUTTON  → INPUT_PULLUP
   - PIN_LED_*   → OUTPUT

2. ライブラリの初期化（使うものだけ）
   - 例: lcd.begin(16, 2)
   - 例: servo.attach(PIN_SERVO)

3. Serial.begin(9600)（デバッグ用）

4. 起動確認（任意）: Red LEDを1秒点灯して消灯
```

**↓ 自分の setup() を設計してください**
```
【処理の流れ】
1. 各ピンのモードを設定する
  - PIN_BUTTON → INPUT_PULLUP
  - PIN_ULTRASONIC_TRIG, PIN_ULTRASONIC_ECHO → OUTPUT/INPUT
  - PIN_7SEG_SER, PIN_7SEG_RCLK, PIN_7SEG_SRCLK, PIN_7SEG_DIGIT1-4, PIN_7SEG_MINUTE → OUTPUT
  - PIN_LED_RED → OUTPUT
2. グローバル変数を初期化する（currentState, elapsedCs, holdCs, など）
3. Serial.begin(9600) でデバッグ出力を有効化
4. 起動確認としてLEDを1秒点灯
```

---

### `loop()` — メインループ

> ※ loop() は「状態ごとに何をするか」だけ書く。細かい処理は各関数に任せる。

```
【処理の流れ】

＜毎ループ実行すること＞
  - 入力を読む（readButton(), readSensor() などを呼ぶ）
  - 現在時刻を取得: now = millis()

＜currentState が 0（待機中）のとき＞
  - センサー値を監視する
  - 検知条件を満たしたら → currentState = 1

＜currentState が 1（動作中）のとき＞
  - メイン処理を行う
  - 終了条件を満たしたら → currentState = 2

＜currentState が 2（完了）のとき＞
  - 完了表示をする
  - リセットボタンが押されたら → currentState = 0

＜currentState が 3（エラー）のとき＞
  - エラー表示をする / リセットを待つ
```

**↓ 自分の loop() を設計してください**
```
【処理の流れ】

＜毎ループ実行すること＞
  - readButtonEdge() でボタン入力を取得
  - readUltrasonicEvent() でセンサ入力を取得
  - 同時入力判定を行い、同時成立時はボタン入力を優先（センサ入力は無効化）
  - 入力イベントを1つに統合して updateStateMachine() へ渡す
  - 現在時刻を取得: now = millis()

＜currentState が 0（停止表示(00.00)）のとき＞
  - 入力イベント（ボタン短押し/センサ検知）で計測中へ遷移
  - 表示を00.00に固定

＜currentState が 1（計測中）＞
  - 10msごとにelapsedCsを加算
  - 入力イベント（ボタン短押し/センサ検知）で停止表示(保持)へ遷移
  - 59.99到達で上限停止へ遷移

＜currentState が 2（停止表示(保持)）＞
  - 計測値を保持して表示
  - 入力イベント（ボタン短押し/センサ検知）で再スタート

＜currentState が 3（上限停止）＞
  - 59.99で固定表示
  - リセット待ち

全状態からボタン長押し（1秒以上）で停止表示(00.00)へ遷移
```

---

### （関数ごとに以下のブロックをコピーして追加してください）

> ※ 基本設計書 2-2 の関数一覧に記載した関数を1つずつ設計します。

---

### `関数名()` — （役割を1行で書く）

---

### `readButtonEdge()` — デバウンス後の短押し/長押しイベントを生成する
**basic_design.md 2-2 との対応：** C01: ボタン読出
**引数：** なし
**戻り値：** InputEvent
```
【処理の流れ】
1. digitalReadでボタン状態を取得
2. 前回状態と異なればlastButtonChangeMsを更新
3. チャタリング判定（30ms未満は無視）
4. 押下開始時刻からの経過で短押し/長押しを判定
5. イベントを返す

【エラー・異常ケース】
- 連打やノイズ時はイベントを発生させない
```

---

### `readUltrasonicEvent()` — 距離を測定し閾値判定からイベント生成する
**basic_design.md 2-2 との対応：** C02: センサー読出
**引数：** なし
**戻り値：** bool
```
【処理の流れ】
1. 超音波センサで距離を取得
2. 3-5cm範囲内なら連続回数をカウント
3. 2回連続一致で有効イベント
4. lastSensorTriggerMsで不感時間管理

【エラー・異常ケース】
- 0cmや400cm超は無効
```

---

### `updateStateMachine()` — 入力イベントに応じて状態を遷移させる
**basic_design.md 2-2 との対応：** C03: 状態更新
**引数：** InputEvent
**戻り値：** なし
```
【処理の流れ】
1. 入力イベントが BUTTON_SHORT / BUTTON_LONG / SENSOR_TRIGGER のどれかを判定
2. 同時入力フラグが立っている場合は BUTTON_* を優先し、SENSOR_TRIGGER は破棄
3. 現在状態と確定イベントで分岐
4. 計測開始/停止/リセット/上限到達の遷移を実施
5. 必要に応じてholdCsやelapsedCsを更新
```

---

### `updateElapsedTime()` — RUN状態のとき10ms単位でelapsedCsを加算する
**basic_design.md 2-2 との対応：** C04: 計時更新
**引数：** なし
**戻り値：** なし
```
【処理の流れ】
1. currentState==計測中のときのみ実行
2. now-lastTickMs>=10msならelapsedCs++
3. lastTickMs=now
4. 59.99超過で上限停止へ
```

---

### `updateDisplay()` — 4桁7セグと1桁7セグを多重化表示する
**basic_design.md 2-2 との対応：** C05: 表示更新
**引数：** なし
**戻り値：** なし
```
【処理の流れ】
1. 現在状態に応じて表示値を決定
2. formatMainDisplay()でSS.hh形式に変換
3. 7セグへ出力
4. minuteOptional有効時は1桁7セグも更新
```

---

### `updateLedPulse()` — ledOffAtMsまでLEDを点灯し期限で消灯する
**basic_design.md 2-2 との対応：** C06: LED更新
**引数：** なし
**戻り値：** なし
```
【処理の流れ】
1. ledOffAtMs>nowならLED点灯
2. それ以外は消灯
```

---

### `formatMainDisplay()` — elapsedCsからSS.hh形式の表示データを作る
**basic_design.md 2-2 との対応：** F01: 59.99まで表示
**引数：** elapsedCs
**戻り値：** DisplayData
```
【処理の流れ】
1. elapsedCsを秒・1/100秒に分割
2. 4桁分のデータを生成
```

---

### `triggerStartStop()` — 入力イベントからRUN⇔STOPを切替える
**basic_design.md 2-2 との対応：** F02: start-stop
**引数：** InputEvent
**戻り値：** なし
```
【処理の流れ】
1. currentStateとイベント種別で分岐
2. RUN→STOP, STOP→RUNの切替
3. pulseLedOnTrigger()を呼ぶ
```

---

### `captureAndHoldTime()` — STOP遷移時に値を保持し表示に反映する
**basic_design.md 2-2 との対応：** F03: 停止値保持
**引数：** なし
**戻り値：** なし
```
【処理の流れ】
1. STOP遷移時にelapsedCsをholdCsへコピー
```

---

### `resetStopwatch()` — elapsedCs/holdCsを0に戻しSTOPへ遷移する
**basic_design.md 2-2 との対応：** F04: 長押しリセット
**引数：** なし
**戻り値：** なし
```
【処理の流れ】
1. 全状態から長押しで呼ばれる
2. elapsedCs, holdCs=0, currentState=停止表示(00.00)
```

---

### `updateMinuteDisplay()` — 60秒超時に1桁7セグへ分を表示する
**basic_design.md 2-2 との対応：** A01: 分表示
**引数：** elapsedCs
**戻り値：** なし
```
【処理の流れ】
1. elapsedCs>=6000なら分表示をON
2. 1桁7セグへ分を出力
```

---

### `pulseLedOnTrigger()` — start/stop発生時に1秒点灯を開始する
**basic_design.md 2-2 との対応：** A02: LED通知
**引数：** なし
**戻り値：** なし
```
【処理の流れ】
1. start/stop時にledOffAtMs=now+1000ms
```

---

## 3. 重要ロジックの詳細設計

### 3-1. チャタリング防止（デバウンス処理）

> ※ ボタンを使う場合は必ず設計してください。

```
【考え方】
  ボタンが押されたとき、30ms 以内の連続入力は「同じ1回の押下」として無視する。

【処理の流れ】
  1. ボタンのデジタル値を読む（digitalRead）
  2. 前回確定した時刻（lastButtonChangeMs）からの経過時間を計算する
  3. 経過時間 < 30ms → 無視する
  4. 経過時間 ≥ 30ms → ボタンの状態として確定する
  5. lastButtonChangeMs を更新する

【必要な変数（Section 1 に追加済みか確認）】
  lastButtonChangeMs : unsigned long   // 前回確定した時刻
  DEBOUNCE_DELAY   : const int = 30  // チャタリング判定時間（ms）
```

---

### 3-2. millis() を使ったタイマー管理

```
【考え方】
  「前回実行した時刻」を記録しておき、「今の時刻 − 前回時刻 ≥ 周期」なら実行する。

【処理の流れ（例: 計時カウントアップ）】
  1. now = millis()
  2. now - lastTickMs >= 10ms かどうか確認
  3. 条件を満たした場合: elapsedCs++、lastTickMs = now
  4. 条件を満たさない場合: 何もしない

【自分のシステムで millis() を使う処理】
  - ボタン監視（1-5ms相当）
  - 超音波センサー読み取り（50ms）
  - 計時カウントアップ（10ms）
  - 7セグ表示の多重化（2ms/桁）
  - LED 1秒点灯制御（1ms監視）
```

---

### 3-3. その他の重要ロジック（任意）

> **【任意】** 複雑なロジックがある場合のみ記入してください。
> 例：「距離に応じたLED点灯パターン」「ゲームの衝突判定」「温度の閾値判定」

```
【処理の流れ】
1. 1ループ内で buttonEvent と sensorEvent を取得する
2. buttonEvent と sensorEvent が同時に true の場合は buttonEvent を採用する
3. sensorEvent のみ true の場合のみ sensorEvent を採用する
4. 採用したイベント1件だけを updateStateMachine() に渡す

【入力値と出力値の関係】
  buttonEvent=true, sensorEvent=true  -> BUTTON優先
  buttonEvent=true, sensorEvent=false -> BUTTON採用
  buttonEvent=false, sensorEvent=true -> SENSOR採用
  buttonEvent=false, sensorEvent=false -> イベントなし

```

---

## 4. デバッグ出力計画（任意）

> **【任意】** 関数設計（Section 2）と並行して記入すると効果的です。
> 「動かない」ときに何を確認すればいいかを事前に計画しておきます。
> 実装後は不要な Serial.println() を削除すること。

| No | 確認したい内容 | 挿入する関数 | Serial.println の内容例 |
|:---|:---|:---|:---|
| 1 | センサー値が正しく取れているか | `readUltrasonicEvent()` | `Serial.println(distanceCm);` |
| 2 | 状態遷移が正しく起きているか | `loop()` | `Serial.println(currentState);` |
| 3 | チャタリング処理が効いているか | `readButtonEdge()` | `Serial.println("btn confirmed");` |
| 4 | 計時値の加算が正しいか | `updateElapsedTime()` | `Serial.println(elapsedCs);` |

---

## 5. 単体テスト仕様書（V字モデル：詳細設計 ↔ 単体テスト）

> ※ 各関数・部品が「単体で正しく動くか」を確認するテスト項目を設計します。
> 「実際の結果」欄は実装後に記入します。

### 5-1. 入力系テスト

| No | テスト対象の関数 | 入力・操作 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | readButtonEdge() | タクトスイッチを1回押す | 短押しイベントが返る |正しく数値が動く、ただし人の目では出力が正しいか判断し兼ねるくらい、数値が見難い | [〇] |
| 2 | readButtonEdge() | スイッチを素早く2回押す | 1回分だけイベントになる |そもそも30ms以内に２回押すことができない | [？] |
| 3 | readUltrasonicEvent() | センサーを正常範囲で使う | 仕様範囲内の値が返る |反応なし | [×] |
| 4 | readUltrasonicEvent() | センサーを遮蔽・範囲外に向ける | 誤動作しない | 反応なし| [×] |
| 5 | updateStateMachine() | 状態遷移イベントを与える | 正しい状態に遷移する |概ね正しい状態に遷移する | [〇] |
| 6 | 入力統合処理 | Button押下とSensor検知を同時発生 | Buttonイベントのみ採用される |Sensorが反応しないのでテスト不可 | [×] |
| 7 | readButtonEdge() | ボタン押下時間 999ms | 長押しではなく短押しとして判定される | 短押し判定| [〇] |
| 8 | readButtonEdge() | ボタン押下時間 1000ms | 長押しとして判定される |長押し判定 | [〇] |
| 9 | readButtonEdge() | ボタン押下時間 1001ms | 長押しとして判定される | 長押し判定、以降長くしても同じ| [〇] |
| 10 | readUltrasonicEvent() | 距離 2.9cm / 3.0cm / 5.0cm / 5.1cm を順に入力 | 3.0-5.0cmのみ有効イベントになる | Sensorが反応しないのでテスト不可| [×] |
| 11 | readUltrasonicEvent() | 距離 0cm（異常値）を入力 | 無効値として破棄されイベント化しない | Sensorが反応しないのでテスト不可| [-] |
| 12 | readUltrasonicEvent() | 距離 400cm超（異常値）を入力 | 無効値として破棄されイベント化しない |Sensorが反応しないのでテスト不可 | [-] |
| 13 | updateStateMachine() | start/stop直後299msで再入力、次に300msで再入力 | 299msは無効、300msは有効 |?| [-] |
| 14 | resetStopwatch() | 全状態（停止初期/計測中/停止保持/上限停止）で長押し | すべて00.00へリセットされる |全てリセットされる | [〇] |

### 5-2. 出力系テスト

| No | テスト対象の関数 | 入力・操作 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | updateDisplay() | state=0（停止表示） | 00.00が表示される | 0000が表示される| [〇] |
| 2 | updateDisplay() | state=1（計測中） | 計測値が増加して表示される | 数値が動くのは確認できる| [△] |
| 3 | updateDisplay() | state=2（停止表示(保持)） | 停止値が保持されて表示される | 保持されていることはわかる| [〇] |
| 4 | updateDisplay() | state=3（上限停止） | 59.99で固定表示される | 99.99が表示される| [×] |
| 5 | updateLedPulse() | start/stop時 | LEDが1秒点灯する |実装しない | [-] |
| 6 | formatMainDisplay() | elapsedCs=0 / 1 / 999 / 1000 / 5999 | 00.00 / 00.01 / 09.99 / 10.00 / 59.99 表示になる | わからない| [×] |
| 7 | updateDisplay() | elapsedCs=5999で1秒経過させる | 表示が59.99のまま固定される |99.99のまま保持される| [△] |

### 5-3. タイミング・並行動作テスト

| No | テスト内容 | テスト手順 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | delay()による処理停止がないか | 計測中にボタンやセンサを操作 | 入力が無視されない | ボタンによる入力は無視されない、センサは反応なしのためわからない| [△] |
| 2 | millis()タイマーの周期精度 | 計測値の増加をストップウォッチで確認 | 10ms単位で正確に加算される |数値が動いてるものの、正しく読み取ることができないためわからない| [×] |
| 3 | 7セグ多重化のちらつき | 7セグ表示を目視確認 | ちらつきが目立たない |最初の停止表示（00.00）は読み取れるが、保持状態で読み取ることが難しい、余計に光っている部分があり読み取りづらい | [×] |
| 4 | 上限境界遷移 | elapsedCs=5998から進める | 5999到達で上限停止へ遷移し、それ以上加算しない |99.99が表示されてしまうが、それ以上加算しない | [△] |
| 5 | センサ測定周期 | 一定距離を保持してログ確認 | SENSOR_INTERVAL_MS（50ms）周期で測定される |センサが反応しない | [×] |

---

## 6. AIレビュー記録

> グループレビューの前に必ず実施してください。

### Q1: 実装上の問題確認

> 「この詳細設計書に書いた関数と処理フローをもとに Arduino でコードを書きます。バグになりやすい箇所・処理の抜け・型の問題はありますか？」

**AIの回答（要約）：**デバウンス・不感時間・異常値処理・状態遷移の抜けを設計段階で明記し、型も適切
注意すべきポイント
・チャタリング処理のタイミング(30msが短すぎるとノイズになる可能性あり、実機での調整)
・状態遷移の抜け（リセット（長押し）で必ず初期状態に戻るか）
・センサ値の異常処理（cmや400cm超など異常値のとき、前回値を保持する処理が抜けていないか確認）
・多重トリガ・不感時間（）
・７セグ表示の多重化
・グローバル変数の初期化


**対応した内容：**

---

### Q2: 単体テスト仕様の確認

> 「Section 5 の単体テスト仕様書で、各関数の動作が正しく検証できていますか？テストが不足している項目や、境界値テストが必要な箇所を教えてください。」

**AIの回答（要約）：**主要な機能・状態遷移・出力・タイミングはテスト項目でカバー済み
以下の点で追加・強化を推奨
・ボタン長押し判定の境界値(1s 未満/ちょうど/超過)
・センサ値の境界テスト（現：3~5cmで反応、3/5cmちょうどor2.9cm/5.1cmなど閾値付近での動作）
・計測値の上限(00000)・下限(5999) (elapsedCs=6000での動作)
・多重トリガ・不感時間(start/stop直後300ms以内の入力が無視されるか)
・異常値・ノイズ処理(センサ値0cm/400cm超、ボタンのノイズ入力時の動作)

**対応した内容：**Section 5 に境界値・異常系・不感時間・上限遷移・全状態リセットの単体テストを追記した。

---

## 7. グループレビュー記録

### 7-1. 指摘一覧

| No | 指摘内容 | 指摘者 | 対応 |
|:---|:---|:---|:---|
| 1 | ピン定義 | 大橋 | ピンを指す位置が自分と違うが、それは合ってるのか |
| 2 |  |  |  |
| 3 |  |  |  |

05/26
| 1 | ピン定義 | コ | Ultrasonic Sensorのピンがどこに配置されているか |
| 2 |  |  |  |

### 7-2. レビューを受けて変更した点

-
-

---

*初版: YYYY-MM-DD / AIレビュー: YYYY-MM-DD / グループレビュー後更新: 2026-05-25*
