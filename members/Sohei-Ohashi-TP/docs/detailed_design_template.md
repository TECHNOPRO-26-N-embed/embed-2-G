# 詳細設計書 — 組込み開発実習

<!-- 作成者: 大橋 荘平 / 日付: 2026-05-25 / グループ: 2-G -->

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
| 作品タイトル | タイマー |
| 状態の種類（1-2 状態遷移から） | 設定中 / 計測中 / 停止中 / 通知中 |
| 実装する関数の数（2-2 関数一覧から） | 11個 |
| グローバル変数の合計バイト数（2-1 SRAM確認から） | 約25B |

---

## 1. グローバル変数・定数の設計

> ※ 基本設計書（2-1 データ設計）をもとに、**型と初期値まで**決めます。
> ここで設計した変数は、この後の関数設計でそのまま使います。

```
【ピン定義】（basic_design.md 3-1 から転記）
  PIN_595_DATA  = 8   // 74HC595 DS
  PIN_595_LATCH = 9   // 74HC595 ST_CP
  PIN_595_CLK   = 10   // 74HC595 SH_CP
  PIN_DIGIT_1   = 2    // 7セグ 桁1
  PIN_DIGIT_2   = 3    // 7セグ 桁2
  PIN_DIGIT_3   = 4    // 7セグ 桁3
  PIN_DIGIT_4   = 5    // 7セグ 桁4
  PIN_BTN_1     = 6    // 開始/停止/リセット（INPUT_PULLUP）
  PIN_BTN_2     = 7    // 通知切替（INPUT_PULLUP）
  PIN_BUZZER    = 11   // アクティブブザー
  PIN_LED_BLUE  = 12   // 青色LED
  PIN_JOY_X     = A0   // ジョイスティックX
  PIN_JOY_Y     = A1   // ジョイスティックY

【状態管理】（basic_design.md 1-2 の状態名から転記）
  STATE_SETTING : byte = 0
  STATE_RUN     : byte = 1
  STATE_STOP    : byte = 2
  STATE_NOTIFY  : byte = 3
  currentState  : byte = STATE_SETTING
  notifyMode    : byte = 0   // 0:ブザー 1:LED

【タイマー（millis()用）】（basic_design.md 2-3 から転記）
  lastBtn1Ms    : unsigned long = 0
  lastBtn2Ms    : unsigned long = 0
  lastJoyMs     : unsigned long = 0
  lastTickMs    : unsigned long = 0
  lastMuxMs     : unsigned long = 0
  lastBlinkMs   : unsigned long = 0

【入力値】
  joyXRaw       : int = 512
  joyYRaw       : int = 512
  btn1Pressed   : bool = false
  btn2Pressed   : bool = false

【時間管理】
  setMinutes    : byte = 0
  setSeconds    : byte = 30
  remainSeconds : unsigned int = 30
  selectedDigit : byte = 0   // 0:分十,1:分一,2:秒十,3:秒一

【その他のフラグ】
  showError     : bool = false

【定数】
  DEBOUNCE_MS        : const unsigned long = 50
  JOY_STEP_MS        : const unsigned long = 120
  COUNTDOWN_MS       : const unsigned long = 1000
  DISPLAY_MUX_MS     : const unsigned long = 2
  NOTIFY_BLINK_MS    : const unsigned long = 250
  JOY_LOW_THRESHOLD  : const int = 400
  JOY_HIGH_THRESHOLD : const int = 620
```

---

## 2. 各関数の詳細設計

> ※ 基本設計書（2-2 関数一覧）で定義した各関数の「中身」を設計します。
> **疑似コード**（日本語＋処理の流れ）で書いてください。実際のC++コードは書かなくてOKです。

---

### `setup()` — 初期化処理

```
【処理の流れ】
1. 7セグ制御ピン、桁選択ピン、ブザー、LEDを OUTPUT に設定する
2. ボタン1・ボタン2を INPUT_PULLUP に設定する
3. ジョイスティック入力（A0/A1）の初期読み取りを行う
4. currentState=STATE_SETTING、notifyMode=0、selectedDigit=0 を設定する
5. remainSeconds = setMinutes * 60 + setSeconds で初期時間を確定する
6. 起動確認として 7セグへ初期値（00:30）を表示する
```

---

### `loop()` — メインループ

> ※ loop() は「状態ごとに何をするか」だけ書く。細かい処理は各関数に任せる。

```
【処理の流れ】

＜毎ループ実行すること＞
  - readInputs() を呼び、ボタン押下イベントとジョイスティック値を更新
  - now = millis() を取得
  - updateStateMachine() を呼び、入力に応じて状態遷移
  - updateCountdown() を呼び、計測中なら1秒減算
  - runNotifyOutput() を呼び、通知状態ならブザー/LED出力を制御
  - updateDisplay() を呼び、MM:SSまたは9999を表示

＜currentState が STATE_SETTING のとき＞
  - handleJoystickSetting() を呼び、桁選択と値変更を行う
  - ボタン1押下で STATE_RUN に遷移

＜currentState が STATE_RUN のとき＞
  - カウントダウンを継続
  - ボタン1押下で STATE_STOP に遷移
  - remainSeconds が0になったら STATE_NOTIFY に遷移

＜currentState が STATE_STOP のとき＞
  - ボタン1押下で setMinutes/setSeconds にリセットし STATE_SETTING に遷移
  - ボタン2押下で notifyMode のみ切替可能

＜currentState が STATE_NOTIFY のとき＞
  - 通知出力を継続
  - ボタン1押下で通知停止し STATE_SETTING に戻る
```

---

### `readInputs()` — ボタンとジョイスティックの入力イベントを作る

**basic_design.md 2-2 との対応：** C01（共通）入力読出

**引数：** なし

**戻り値：** InputEvent（btn1Edge, btn2Edge, joyX, joyY）

```
【処理の流れ】
1. PIN_BTN_1 / PIN_BTN_2 を読み、押下エッジを判定する
2. 押下エッジ判定時、lastBtn1Ms / lastBtn2Ms と DEBOUNCE_MS でデバウンスする
3. A0 / A1 を読み、joyXRaw / joyYRaw を更新する
4. InputEvent 構造体に結果を格納して返す

【エラー・異常ケース】
- 読み値が不安定な場合: 直前値を維持し、イベントを発生させない
```

---

### `updateStateMachine()` — 入力イベントから状態遷移する

**basic_design.md 2-2 との対応：** C02（共通）状態更新

**引数：** InputEvent event

**戻り値：** void

```
【処理の流れ】
1. event.btn2Edge が true なら toggleNotifyMode() を呼ぶ
2. currentState が STATE_SETTING のとき
   - event.btn1Edge なら STATE_RUN へ遷移し lastTickMs=millis() を設定
3. currentState が STATE_RUN のとき
   - event.btn1Edge なら STATE_STOP へ遷移
4. currentState が STATE_STOP のとき
   - event.btn1Edge なら handleButton1Action() を呼び STATE_SETTING へ戻す
5. currentState が STATE_NOTIFY のとき
   - event.btn1Edge なら通知停止して STATE_SETTING へ戻す

【エラー・異常ケース】
- 不正状態値の場合: STATE_SETTING に戻し showError=true とする
```

---

### `updateCountdown()` — 計測中の残り時間を減算する

**basic_design.md 2-2 との対応：** C03（共通）計時更新 / F01 必須機能①

**引数：** なし

**戻り値：** bool（0到達したら true）

```
【処理の流れ】
1. currentState が STATE_RUN 以外なら false を返す
2. now=millis() を取得し、now-lastTickMs >= COUNTDOWN_MS を確認
3. 条件成立時に remainSeconds を1減らし、lastTickMs=now を更新
4. remainSeconds が0になったら STATE_NOTIFY に遷移して true を返す
5. それ以外は false を返す

【エラー・異常ケース】
- remainSeconds が負相当になるケース: 0に補正し STATE_NOTIFY へ遷移
```

---

### `updateDisplay()` — 4桁7セグへ表示データを出す

**basic_design.md 2-2 との対応：** C04（共通）表示更新

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. showError が true の場合は showError9999() を呼ぶ
2. それ以外は表示対象秒数を MM:SS に分解する
3. DISPLAY_MUX_MS 周期で桁を1つずつ切替えて多重化表示
4. 設定中は selectedDigit を点滅させて編集中桁を示す

【エラー・異常ケース】
- 分または秒が範囲外の場合: showError=true にして9999表示
```

---

### `runNotifyOutput()` — 通知状態の出力を制御する

**basic_design.md 2-2 との対応：** C05（共通）通知更新

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. currentState が STATE_NOTIFY 以外なら buzzer/LED をOFFにする
2. notifyMode==0 のとき: ブザーをON、LEDをOFF
3. notifyMode==1 のとき: ブザーをOFF、LEDを NOTIFY_BLINK_MS 周期で点滅

【エラー・異常ケース】
- notifyMode が不正値: 両方OFFにして showError=true
```

---

### `handleJoystickSetting()` — 設定時間を編集する

**basic_design.md 2-2 との対応：** F02 必須機能②

**引数：** InputEvent event

**戻り値：** void

```
【処理の流れ】
1. currentState が STATE_SETTING でない場合は何もしない
2. now-lastJoyMs >= JOY_STEP_MS のときのみ入力を反映
3. X方向で selectedDigit を左右移動（0〜3で循環）
4. Y方向で selectedDigit に対応する桁を増減
5. 更新後に setMinutes/setSeconds を 00:00〜59:59 に正規化
6. remainSeconds = setMinutes * 60 + setSeconds を更新

【エラー・異常ケース】
- 正規化不能な値: showError=true として9999表示
```

---

### `handleButton1Action()` — 開始/停止/リセット動作を切替える

**basic_design.md 2-2 との対応：** F03 必須機能③

**引数：** InputEvent event

**戻り値：** void

```
【処理の流れ】
1. event.btn1Edge が false なら何もしない
2. STATE_SETTING なら STATE_RUN に遷移
3. STATE_RUN なら STATE_STOP に遷移
4. STATE_STOP なら setMinutes/setSeconds へ値を戻し STATE_SETTING へ遷移
5. STATE_NOTIFY なら通知停止して STATE_SETTING へ遷移

【エラー・異常ケース】
- 不正状態値の場合: STATE_SETTING に戻す
```

---

### `toggleNotifyMode()` — 通知方式を切替える

**basic_design.md 2-2 との対応：** A01 追加機能①

**引数：** InputEvent event

**戻り値：** void

```
【処理の流れ】
1. event.btn2Edge が true のときのみ処理
2. notifyMode を 0↔1 で切替
3. 切替直後に出力を再設定（不要側をOFF）

【エラー・異常ケース】
- notifyMode が0/1以外: 0に戻す
```

---

### `showError9999()` — エラー時の固定表示を行う

**basic_design.md 2-2 との対応：** E01 例外処理

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. 4桁の表示データを 9,9,9,9 に固定する
2. 7セグ多重化表示で9999を継続表示する
3. ボタン1押下で showError=false に戻す

【エラー・異常ケース】
- 表示処理失敗時: すべての出力をOFFにして安全停止
```

---

## 3. 重要ロジックの詳細設計

### 3-1. チャタリング防止（デバウンス処理）

> ※ ボタンを使う場合は必ず設計してください。

```
【考え方】
  ボタンが押されたとき、50ms 以内の連続入力は「同じ1回の押下」として無視する。

【処理の流れ】
  1. ボタンのデジタル値を読む（digitalRead）
  2. 前回確定した時刻（lastBtn1Ms / lastBtn2Ms）からの経過時間を計算する
  3. 経過時間 < DEBOUNCE_MS（50ms）→ 無視する
  4. 経過時間 ≥ DEBOUNCE_MS かつ LOW遷移時のみ押下エッジ確定
  5. lastBtn1Ms / lastBtn2Ms を更新する

【必要な変数（Section 1 に追加済みか確認）】
  lastBtn1Ms  : unsigned long
  lastBtn2Ms  : unsigned long
  DEBOUNCE_MS : const unsigned long = 50
```

---

### 3-2. millis() を使ったタイマー管理

```
【考え方】
  「前回実行した時刻」を記録しておき、「今の時刻 − 前回時刻 ≥ 周期」なら実行する。

【処理の流れ（例: 通知LED点滅）】
  1. now = millis()
  2. now - lastBlinkMs >= NOTIFY_BLINK_MS かどうか確認
  3. 条件を満たした場合: LEDのON/OFFを切り替え、lastBlinkMs = now
  4. 条件を満たさない場合: 何もしない（次のループで再チェック）

【自分のシステムで millis() を使う処理】
  - カウントダウン減算: 1000ms（lastTickMs）
  - 7セグ多重化: 2ms（lastMuxMs）
  - ジョイスティック反映: 120ms（lastJoyMs）
  - 通知LED点滅: 250ms（lastBlinkMs）
```

---

### 3-3. その他の重要ロジック（任意）

> **【任意】** 複雑なロジックがある場合のみ記入してください。
> 例：「距離に応じたLED点灯パターン」「ゲームの衝突判定」「温度の閾値判定」

```
【処理の流れ】
1. ジョイスティックXで selectedDigit を循環移動する（0→1→2→3→0）
2. ジョイスティックYで選択中桁の値を増減する
3. setMinutes/setSeconds を 00:00〜59:59 で正規化し remainSeconds を再計算する

【入力値と出力値の関係】
  入力: joyXRaw / joyYRaw / selectedDigit
  出力: setMinutes / setSeconds / remainSeconds
```

---

## 4. デバッグ出力計画（任意）

> **【任意】** 関数設計（Section 2）と並行して記入すると効果的です。
> 「動かない」ときに何を確認すればいいかを事前に計画しておきます。
> 実装後は不要な Serial.println() を削除すること。

| No | 確認したい内容 | 挿入する関数 | Serial.println の内容例 |
|:---|:---|:---|:---|
| 1 | ジョイスティック値が正しく読めるか | `readInputs()` | `Serial.println(joyXRaw); Serial.println(joyYRaw);` |
| 2 | 状態遷移が正しく起きているか | `updateStateMachine()` | `Serial.println(currentState);` |
| 3 | カウントダウン周期が1秒か | `updateCountdown()` | `Serial.println(remainSeconds);` |
| 4 | 通知モードが切り替わるか | `toggleNotifyMode()` | `Serial.println(notifyMode);` |

---

## 5. 単体テスト仕様書（V字モデル：詳細設計 ↔ 単体テスト）

> ※ 各関数・部品が「単体で正しく動くか」を確認するテスト項目を設計します。
> 「実際の結果」欄は実装後に記入します。

### 5-1. 入力系テスト

| No | テスト対象の関数 | 入力・操作 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | readInputs() | ボタン1を1回押す | btn1Edge=true が1回だけ立つ | | [ ] |
| 2 | readInputs() | ボタン2を素早く2回押す | デバウンスで1回分のみ有効 | | [ ] |
| 3 | handleJoystickSetting() | Xを右へ倒す | selectedDigit が+1される | | [ ] |
| 4 | handleJoystickSetting() | Yを上へ倒す | 選択中桁の値が+1される | | [ ] |
| 5 | handleJoystickSetting() | 59:59から増加操作 | 59:59を維持し範囲外にならない | | [ ] |

### 5-2. 出力系テスト

| No | テスト対象の関数 | 入力・操作 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | updateDisplay() | remainSeconds=125 を設定 | 02:05 が表示される | | [ ] |
| 2 | runNotifyOutput() | STATE_NOTIFY + notifyMode=0 | ブザーON、LED OFF | | [ ] |
| 3 | runNotifyOutput() | STATE_NOTIFY + notifyMode=1 | ブザーOFF、LED点滅 | | [ ] |
| 4 | showError9999() | showError=true | 9999 が表示される | | [ ] |

### 5-3. タイミング・並行動作テスト

| No | テスト内容 | テスト手順 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | delay()による処理停止がないか | 通知点滅中にボタン1を押す | すぐに通知停止し設定中へ戻る | | [ ] |
| 2 | カウントダウン周期精度 | 00:10から計測し10秒を計測 | 実時間10秒前後で00:00になる | | [ ] |
| 3 | 7セグ多重化安定性 | 全桁表示を30秒観察 | ちらつきが目視で気にならない | | [ ] |

---

## 6. AIレビュー記録

> グループレビューの前に必ず実施してください。

### Q1: 実装上の問題確認

> 「この詳細設計書に書いた関数と処理フローをもとに Arduino でコードを書きます。バグになりやすい箇所・処理の抜け・型の問題はありますか？」

**AIの回答（要約）：**
- ボタンの押下エッジ判定は「現在値」と「前回値」の比較を入れないと多重反応しやすい。
- remainSeconds と setMinutes/setSeconds の同期更新漏れが起きやすい。
- selectedDigit の範囲チェック（0〜3）を必ず入れるべき。

**対応した内容：**
- readInputs() に押下エッジ判定と50msデバウンスを明記。
- handleJoystickSetting() の最後に remainSeconds 再計算を追加。
- selectedDigit を循環処理（0〜3）で管理する設計にした。

---

### Q2: 単体テスト仕様の確認

> 「Section 5 の単体テスト仕様書で、各関数の動作が正しく検証できていますか？テストが不足している項目や、境界値テストが必要な箇所を教えてください。」

**AIの回答（要約）：**
- 00:00、59:59、通知モード切替直後の境界テストを追加すると良い。
- 7セグ多重化のちらつき確認を単体テストに入れると実装品質が上がる。

**対応した内容：**
- 5-1 に 59:59上限の入力テストを追加。
- 5-2 に notifyMode別出力テストを追加。
- 5-3 に 7セグ多重化安定性テストを追加。

---

## 7. グループレビュー記録

### 7-1. 指摘一覧

| No | 指摘内容 | 指摘者 | 対応 |
|:---|:---|:---|:---|
| 1 | 基本設計書の段階では、停止時に時間を増やすことは不可能とすると言っていたが、今回の内容では可能となっていたが何故なのか。 | コ ミョングン| 初めはできないようにするつもりだったが、考えた結果、自分の作りたいものの機能としては可能であるべきだと考えたから。 |
| 2 | エラー時にLEDとブザーをONにする。 | 佐藤 瑶士 | 自分が思いつかなかった発想だったので、いいアイデアだと思った。ただ、今回作りたいものの機能としてそこまで重要ではないと判断し、追加しなかった。 |
| 3 |  |  |  |

### 7-2. レビューを受けて変更した点

- 特になし
---

*初版: 2026-05-25 / AIレビュー: 2026-05-25 / グループレビュー後更新: 未実施*
