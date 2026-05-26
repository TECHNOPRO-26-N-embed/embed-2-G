# 詳細設計書 — 組込み開発実習

<!-- 作成者: 萩原大翔 / 日付: 2026-05-25 / グループ: 2-G -->

> **このドキュメントの目的**
> 基本設計書・要件定義書で決めた構造を、実装できる粒度まで具体化する。
> 本書の疑似コードをそのまま Arduino スケッチへ落とせる状態を目標とする。

---

## 0. 基本設計書・要件定義書との接続確認

| 項目 | 転記 |
|:--|:--|
| 作品タイトル | 手持無沙汰解消スロット |
| 一言説明 | ボタンが四つあり押すと数字が高速で回りそれぞれ対応するボタンを押すとリールが止まる |
| 使用共通部品 | 4Digit 7-Segment Display |
| 追加部品 | スティック、ボタン、LEDライト |
| 必須機能数 | 3件 |
| 追加機能数 | 1件（ペカる） |
| 実装する関数の数 | 8個 |

---

## 1. グローバル変数・定数の設計

```
【ピン定義】
  PIN_STICK_ANALOG = A0  // スティックY軸（アナログ入力）
  PIN_STOP_1      = 3    // リール停止ボタン1
  PIN_STOP_2      = 4    // リール停止ボタン2
  PIN_STOP_3      = 5    // リール停止ボタン3
  PIN_LED_1       = 6    // LED1
  PIN_LED_2       = 7    // LED2
  PIN_LED_3       = 8    // LED3
  PIN_TM1637_CLK  = 9    // 7セグ CLK
  PIN_TM1637_DIO  = 10   // 7セグ DIO

【状態定義】
  STATE_WAIT      = 0    // 待機中
  STATE_SPINNING  = 1    // 抽選中
  STATE_STOPPING  = 2    // リール停止中
  STATE_RESULT    = 3    // 結果表示中（1秒保持）

【入力構造体】
  InputState:
    startPressed  : bool
    stop1Pressed  : bool
    stop2Pressed  : bool
    stop3Pressed  : bool

【基本変数】
  currentState    : byte = STATE_WAIT
  isWinning       : bool = false
  stickYValue     : int = 512   // スティックY軸の現在値（0-1023）
  startActivePrev : bool = false   // 前ループの開始入力アクティブ状態

【リール管理】
  reels[3]        : byte = {0, 0, 0}
  fixed[3]        : bool = {false, false, false}
  stopCount       : byte = 0
  WIN_NUMBER      : const byte = 7
  REEL_COUNT      : const byte = 3
  MAX_DIGIT       : const byte = 9

【デバウンス】
  DEBOUNCE_MS     : const unsigned long = 50
  lastStartMs     : unsigned long = 0
  lastStop1Ms     : unsigned long = 0
  lastStop2Ms     : unsigned long = 0
  lastStop3Ms     : unsigned long = 0

【millisタイマ】
  REEL_UPDATE_MS  : const unsigned long = 50
  LED_CHASE_MS    : const unsigned long = 200
  RESULT_HOLD_MS  : const unsigned long = 1000
  lastReelMs      : unsigned long = 0
  lastLedMs       : unsigned long = 0
  resultEndMs     : unsigned long = 0
  ledIndex        : byte = 0    // 0:LED1, 1:LED2, 2:LED3
```

---

## 2. 各関数の詳細設計

### setup() — 初期化処理
```
1. 各ピンモードを設定
  - pinMode(PIN_STICK_ANALOG, INPUT)  // A0アナログ入力
  - pinMode(PIN_STOP_1, INPUT_PULLUP)
  - pinMode(PIN_STOP_2, INPUT_PULLUP)
  - pinMode(PIN_STOP_3, INPUT_PULLUP)
  - pinMode(PIN_LED_1, OUTPUT)
  - pinMode(PIN_LED_2, OUTPUT)
  - pinMode(PIN_LED_3, OUTPUT)
2. 7セグ表示ライブラリ初期化
3. 乱数初期化
4. 状態変数・リール・フラグ初期化
5. LED1→LED2→LED3 を約0.2秒（200ms）間隔で順番に点灯し、全消灯して待機
```

### loop() — メインループ
```
・now = millis() を取得
・readInputs() で入力取得
・updateEffects(currentState, isWinning) を呼ぶ

＜STATE_WAIT＞
  - 0-0-0表示維持
  - handleStartInput() で開始成立時:
      isWinning決定、fixed[]初期化、stopCount=0、currentState=STATE_SPINNING

＜STATE_SPINNING＞
  - updateReelsAndDisplay(now)で未停止リールを50ms周期で更新
  - handleStopInput()で停止ボタンに対応するリール確定
  - 1つでも停止が入ったら currentState=STATE_STOPPING

＜STATE_STOPPING＞
  - updateReelsAndDisplay(now)継続（未停止リールのみ回す）
  - handleStopInput()継続
  - stopCount==REEL_COUNTでrunResultLighting(now)実行、currentState=STATE_RESULT

＜STATE_RESULT＞
  - 結果表示（LED点灯）を維持
  - now >= resultEndMs で全LED消灯し、currentState=STATE_WAIT
```

### readInputs() — 入力状態を読み取る
```
1. スティックY軸を analogRead(PIN_STICK_ANALOG) で取得し stickYValue に格納
2. startActiveNow = (stickYValue <= 800) を計算（-Y方向に倒した判定）
3. startPressed = (startActiveNow==true && startActivePrev==false) とする（立ち上がりエッジで1回だけ成立）
4. startActivePrev = startActiveNow に更新
5. 各ボタン入力ピンをdigitalRead()
6. LOWを「押下」としてboolに正規化
7. InputState構造体にstartPressed/stop1Pressed/stop2Pressed/stop3Pressed格納
8. return
```

### handleStartInput() — 抽選開始入力を処理
```
1. startPressed==falseならfalse
2. now-lastStartMs<DEBOUNCE_MSならfalse
3. lastStartMs=now
4. isWinning=(random(0,30)==0)で当選判定（当たり確率1/30）
5. リール初期化
6. true返す

【備考】
・スティックを倒しっぱなしでも開始は1回だけ成立する
```

### handleStopInput() — リール停止入力を処理
```
1. added=0
2. stop1Pressed成立 & fixed[0]==false & (now-lastStop1Ms)>=DEBOUNCE_MS なら
  lastStop1Ms=now, fixed[0]=true, reels[0]=当選?7:乱数, stopCount++, added++
3. stop2/stop3も同様に lastStop2Ms/lastStop3Ms を使って判定
4. return added
```

### updateReelsAndDisplay() — リール数字と表示を更新
```
1. nowMs-lastReelMs<REEL_UPDATE_MSならreturn
2. lastReelMs=nowMs
3. fixed[i]==falseのリールを乱数で更新
4. 7セグに反映
```

### updateEffects() — 状態に応じたLED演出を更新
```
1. STATE_WAIT:全LED消灯
2. STATE_SPINNING/STOPPING:200msごとに ledIndex を 0→1→2→0 と進め、LEDを順番に1つずつ点灯
3. 結果演出はrunResultLighting(now)で確定
```

### runResultLighting() — 結果表示時のLED確定演出
```
1. isWinning==true:LED3つ同時点灯
2. isWinning==false:LED1のみ点灯
3. resultEndMs = now + RESULT_HOLD_MS を設定（1秒保持）
4. 消灯はSTATE_RESULT側で now>=resultEndMs のときに実行
```

---

## 3. 重要ロジックの詳細設計

### チャタリング防止（デバウンス処理）
```
・前回確定から50ms以上経過で押下成立
・start/stop1/stop2/stop3全てに適用
```

### millis()を使ったタイマー管理
```
・delay()を使わず差分時間で周期制御
・リール更新50ms、LED順送り200ms
```

### スロット抽選ロジック
```
・開始時にisWinning確定
・停止時に各桁の最終値確定
・当選時は全桁7、非当選時は乱数
・3桁停止後は結果表示を1秒保持して1ゲーム終了
```

---

## 4. デバッグ出力計画

| No | 確認内容 | 関数 | Serial.println例 |
|:---|:---|:---|:---|
| 1 | 開始入力が1回だけ確定 | handleStartInput() | START accepted |
| 2 | 状態遷移が想定通り | loop() | state= + currentState |
| 3 | 停止ボタンが重複確定しない | handleStopInput() | stopCount= + stopCount |
| 4 | 当選判定と最終表示の対応 | handleStartInput()/handleStopInput() | win=1, reel0=7 |

---

## 5. 単体テスト仕様書

### 入力系テスト
| No | 関数 | 入力・操作 | 期待結果 | 実際 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | handleStartInput() | スティック1回押し | trueが1回だけ返る | | [ ] |
| 2 | handleStartInput() | 50ms未満で連打 | 2回目以降は無視 | | [ ] |
| 3 | handleStopInput() | stop1押し | reels[0]のみ停止 | | [ ] |
| 4 | handleStopInput() | 同じ停止ボタン再押下 | stopCount増えない | | [ ] |
| 5 | readInputs() | 何も押さない | 全てfalse | | [ ] |
| 6 | readInputs() | スティックを倒しっぱなし | startPressedは最初の1回のみtrue | | [ ] |

### 出力系テスト
| No | 関数 | 入力・操作 | 期待結果 | 実際 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | updateReelsAndDisplay() | 抽選中で未停止3桁 | 50msごとに数字変化 | | [ ] |
| 2 | runResultLighting() | isWinning=true | LED3つ同時点灯 | | [ ] |
| 3 | runResultLighting() | isWinning=false | LED1のみ点灯 | | [ ] |

### タイミング・並行動作テスト
| No | テスト内容 | 手順 | 期待結果 | 実際 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | delay()による処理停止がないか | 回転中に停止ボタン押す | 即時に停止反映 | | [ ] |
| 2 | リール更新周期確認 | シリアルで更新時刻差表示 | 約50ms間隔 | | [ ] |
| 3 | LED順送り周期確認 | LED1→2→3の切替間隔を計測 | 約200ms間隔 | | [ ] |
| 4 | 結果表示保持時間確認 | 3停止直後から待機復帰まで計測 | 約1000ms保持後にWAIT復帰 | | [ ] |

---

## 6. AIレビュー記録

### Q1: 実装上の問題確認
> 「この詳細設計書に書いた関数と処理フローをもとに Arduino でコードを書きます。バグになりやすい箇所・処理の抜け・型の問題はありますか？」
**AIの回答（要約）：**
開始・停止入力のデバウンス分離は妥当。状態遷移終端も定義済み。表示器ピン定義をグループで先に確定すると実装ブレ減。
**対応内容：**
TM1637想定のCLK/DIOを明記、停止完了時の待機復帰をloop()に明記。

### Q2: 単体テスト仕様の確認
> 「Section 5 の単体テスト仕様書で、各関数の動作が正しく検証できていますか？テストが不足している項目や、境界値テストが必要な箇所を教えてください。」
**AIの回答（要約）：**
主要関数はカバー。境界値として「デバウンス49ms/50ms」と「3停止目入力直後の状態遷移」を追加するとさらに良い。
**対応内容：**
入力系テストにデバウンス連打ケースを入れ、並行動作テストで停止反映の即時性を確認項目化。

---

## 7. グループレビュー記録

| No | 指摘内容 | 指摘者 | 対応 |
|:---|:---|:---|:---|
| 1 |  |  |  |
| 2 |  |  |  |
| 3 |  |  |  |

### レビューを受けて変更した点
- （レビュー後に追記）
- （レビュー後に追記）

---

*初版: 2026-05-25 / AIレビュー: 2026-05-25 / グループレビュー後更新: YYYY-MM-DD*