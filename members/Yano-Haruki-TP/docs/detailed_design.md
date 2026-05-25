# 詳細設計書 — 組込み開発実習

<!-- 作成者: 矢野元暉 / 日付: 2026-05-25 / グループ: 2-G -->

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
| 作品タイトル | 温度が見えるモニター |
| 状態の種類（1-2 状態遷移から） | 温度監視中 / 高温警告 / 低温警告 |
| 実装する関数の数（2-2 関数一覧から） | 8個 |
| グローバル変数の合計バイト数（2-1 SRAM確認から） | 約28B（主要変数） |

---

## 1. グローバル変数・定数の設計

> ※ 基本設計書（2-1 データ設計）をもとに、**型と初期値まで**決めます。
> ここで設計した変数は、この後の関数設計でそのまま使います。

```
【ピン定義】（basic_design.md 3-1 から転記）
  PIN_DHT        = 2    // DHT11データピン
  PIN_LED_RED    = 3    // 赤LED（高温）
  PIN_LED_BLUE   = 4    // 青LED（低温）
  PIN_BUZZER     = 5    // パッシブブザー（任意）
  PIN_SEG_DIO    = 10   // 7セグ表示データ
  PIN_SEG_CLK    = 9    // 7セグ表示クロック
  PIN_SEG_LATCH  = 8    // 7セグ表示ラッチ

【状態管理】（basic_design.md 1-2 の状態名から転記）
  currentState   : int = 0   // 0:温度監視中 1:高温警告 2:低温警告

【タイマー（millis()用）】（basic_design.md 2-3 から転記）
  lastUpdateMillis : unsigned long = 0
  lastBuzzerMillis : unsigned long = 0
  TEMP_INTERVAL    : const unsigned long = 300000   // 5分
  BUZZER_INTERVAL  : const unsigned long = 500      // 0.5秒（断続鳴動）

【センサー・入力値】（basic_design.md 2-1 から転記）
  temperature    : float = 0.0
  humidity       : float = 0.0
  displayValue   : int   = 0

【その他のフラグ・カウンター】
  TEMP_HIGH_TH   : const int = 28
  TEMP_LOW_TH    : const int = 20
  sensorError    : bool = false
  buzzerEnable   : bool = true
  showHumidity   : bool = false
```

---

## 2. 各関数の詳細設計

> ※ 基本設計書（2-2 関数一覧）で定義した各関数の「中身」を設計します。
> **疑似コード**（日本語＋処理の流れ）で書いてください。実際のC++コードは書かなくてOKです。

---

### `setup()` — 初期化処理

```
【処理の流れ】
1. ピンモード設定
   - PIN_LED_RED, PIN_LED_BLUE, PIN_BUZZER, PIN_SEG_* を OUTPUT
   - PIN_DHT はライブラリ側初期化を使用
2. DHT11と7セグ表示ライブラリを初期化
3. 起動確認
   - 赤/青LEDを各200ms点灯して消灯
   - ブザーを100msだけ鳴らし停止
4. currentState = 0（温度監視中）に設定
5. lastUpdateMillis = 0 で初回はすぐ温湿度を取得可能にする
```

---

### `loop()` — メインループ

> ※ loop() は「状態ごとに何をするか」だけ書く。細かい処理は各関数に任せる。

```
【処理の流れ】

＜毎ループ実行すること＞
  - now = millis() を取得
  - TEMP_INTERVAL 経過時に readTemperature() を実行
  - updateState() で currentState を更新
  - displayTemperature() を実行（showHumidity=trueなら湿度表示）
  - updateLED() を実行
  - updateBuzzer() を実行（buzzerEnable=trueの場合のみ）

＜currentState が 0（温度監視中）のとき＞
  - 赤/青LEDを消灯
  - ブザー停止

＜currentState が 1（高温警告）のとき＞
  - 赤LED点灯、
  - BUZZER_INTERVALで断続鳴動　//可能であれば

＜currentState が 2（低温警告）のとき＞
  - 青LED点灯、
  - BUZZER_INTERVALで断続鳴動　//可能であれば

＜sensorError が true のとき＞
  - 7セグにエラーコード（例: EEEE）を表示
  - LEDとブザーは安全側として停止
```

---

### `readTemperature()` — DHT11から温湿度を取得する

**basic_design.md 2-2 との対応：** 温度測定

**引数：** なし

**戻り値：** float（温度）

```
【処理の流れ】
1. DHT11から温度と湿度を読み取る
2. 取得値がNaN、または仕様範囲外なら sensorError=true にする
3. 正常値なら temperature/humidity を更新し sensorError=false にする
4. 温度を戻り値として返す

【エラー・異常ケース】
- 読み取り失敗時: 前回のtemperature/humidityを保持し、sensorError=true
```

---

### `updateState()` — 温度に応じて状態を更新する

**basic_design.md 2-2 との対応：** LED警告（状態判定）

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. sensorError=true なら currentState は変更しない
2. temperature >= 28 なら currentState=1（高温警告）
3. temperature <= 20 なら currentState=2（低温警告）
4. 20 < temperature < 28 なら currentState=0（温度監視中）

【エラー・異常ケース】
- 温度が未更新の場合: 前回状態を維持
```

---

### `displayTemperature()` — 7セグへ表示値を出力する

**basic_design.md 2-2 との対応：** 温度表示

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. sensorError=true ならエラーコードを表示
2. showHumidity=false なら temperature を整数化して表示
3. showHumidity=true なら humidity を整数化して表示
4. 負値や3桁超えは表示可能範囲に丸める

【エラー・異常ケース】
- 表示範囲外: 最小/最大表示値にクリップ
```

---

### `updateLED()` — 現在状態に応じてLEDを制御する

**basic_design.md 2-2 との対応：** LED警告

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. sensorError=true なら赤青とも消灯
2. currentState=1 なら赤ON/青OFF
3. currentState=2 なら青ON/赤OFF
4. currentState=0 なら赤青ともOFF

【エラー・異常ケース】
- 状態値異常: 安全側として赤青ともOFF
```

---

### `updateBuzzer()` — 警告時にブザーを鳴動する

**basic_design.md 2-2 との対応：** ブザー警告（任意）

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. buzzerEnable=false または sensorError=true なら停止
2. currentState が 1 または 2 のときのみ鳴動対象
3. now-lastBuzzerMillis >= BUZZER_INTERVAL でON/OFFを切替
4. 警告解除時は必ずOFFに戻す

【エラー・異常ケース】
- タイマー異常: ブザーをOFF固定
```

---

### `displayHumidity()` — 湿度表示モードを処理する

**basic_design.md 2-2 との対応：** 湿度表示（

**引数：** なし

**戻り値：** void

```
【処理の流れ】
1. showHumidity=true のときのみ呼ぶ
2. humidity を整数化し 7セグへ表示
3. 一定時間後に showHumidity=false に戻す

【エラー・異常ケース】
- 湿度値異常: 表示を更新せず温度表示へ戻す
```

---

## 3. 重要ロジックの詳細設計

### 3-1. チャタリング防止（デバウンス処理）

> ※ ボタンを使う場合は必ず設計してください。

```
【考え方】
  本作品はボタン入力を使わないため、デバウンス処理は実装対象外とする。
  将来ボタンで表示切替を追加する場合に備えて設計のみ残す。

【処理の流れ】
  1. （現行版）処理なし
  2. 将来追加時: digitalReadで入力取得
  3. 50ms未満の連続入力を無視
  4. 50ms以上経過した入力のみ確定

【必要な変数（Section 1 に追加済みか確認）】
  lastDebounceTime : unsigned long = 0
  DEBOUNCE_DELAY   : const int = 50
```

---

### 3-2. millis() を使ったタイマー管理

```
【考え方】
  「前回実行した時刻」を記録しておき、「今の時刻 − 前回時刻 ≥ 周期」なら実行する。

【処理の流れ（本システム）】
  1. now = millis()
  2. now - lastUpdateMillis >= TEMP_INTERVAL なら readTemperature() を実行
  3. 実行後に lastUpdateMillis = now
  4. 警告状態中は now - lastBuzzerMillis >= BUZZER_INTERVAL でブザーON/OFF切替
  5. それ以外は次ループで再判定

【自分のシステムで millis() を使う処理】
  - 温度測定・表示更新: 5分周期
  - ブザー断続鳴動: 0.5秒周期
  - LED制御: ループ毎に状態連動で即時反映
```

---

### 3-3. その他の重要ロジック（任意）

> **【任意】** 複雑なロジックがある場合のみ記入してください。
> 例：「距離に応じたLED点灯パターン」「ゲームの衝突判定」「温度の閾値判定」

```
【処理の流れ】
1. 温度閾値判定にヒステリシスを導入する
2. 高温遷移: temperature >= 28、解除: temperature <= 27
3. 低温遷移: temperature <= 20、解除: temperature >= 21
4. 判定条件の順序を固定して状態の揺れを防ぐ

【入力値と出力値の関係】
temperature >= 28      -> currentState = 高温警告
temperature <= 20      -> currentState = 低温警告
21 <= temperature <=27 -> currentState = 温度監視中
```

---

## 4. デバッグ出力計画（任意）

> **【任意】** 関数設計（Section 2）と並行して記入すると効果的です。
> 「動かない」ときに何を確認すればいいかを事前に計画しておきます。
> 実装後は不要な Serial.println() を削除すること。

| No | 確認したい内容 | 挿入する関数 | Serial.println の内容例 |
|:---|:---|:---|:---|
| 1 | 温湿度値が正しく取れているか | `readTemperature()` | `Serial.println(temperature);` |
| 2 | 状態遷移が正しく起きているか | `updateState()` | `Serial.println(currentState);` |
| 3 | エラー判定が動くか | `readTemperature()` | `Serial.println(sensorError);` |
| 4 | ブザー周期が正しいか | `updateBuzzer()` | `Serial.println(lastBuzzerMillis);` |

---

## 5. 単体テスト仕様書（V字モデル：詳細設計 ↔ 単体テスト）

> ※ 各関数・部品が「単体で正しく動くか」を確認するテスト項目を設計します。
> 「実際の結果」欄は実装後に記入します。

### 5-1. 入力系テスト

| No | テスト対象の関数 | 入力・操作 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | readTemperature() | 通常環境で読み取り | 10〜35℃程度の妥当な値が返る | | [ ] |
| 2 | readTemperature() | センサーを外す/失敗させる | sensorError=true になる | | [ ] |
| 3 | updateState() | 温度=29を入力 | currentState=1（高温警告） | | [ ] |
| 4 | updateState() | 温度=19を入力 | currentState=2（低温警告） | | [ ] |
| 5 | updateState() | 温度=24を入力 | currentState=0（温度監視中） | | [ ] |

### 5-2. 出力系テスト

| No | テスト対象の関数 | 入力・操作 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | updateLED() | currentState=1 | 赤LED点灯、青LED消灯 | | [ ] |
| 2 | updateLED() | currentState=2 | 青LED点灯、赤LED消灯 | | [ ] |
| 3 | updateBuzzer() | 警告状態を維持 | 0.5秒周期で断続鳴動する | | [ ] |

### 5-3. タイミング・並行動作テスト

| No | テスト内容 | テスト手順 | 期待する結果 | 実際の結果 | 合否 |
|:---|:---|:---|:---|:---|:---|
| 1 | delay()による処理停止がないか | 警告中に温度を閾値内へ戻す | 次ループで警告解除される | | [ ] |
| 2 | millis()タイマーの周期精度 | 5分更新を時計で確認 | 温度更新が約300秒周期で実行される | | [ ] |

---

## 6. AIレビュー記録

> グループレビューの前に必ず実施してください。

### Q1: 実装上の問題確認

> 「この詳細設計書に書いた関数と処理フローをもとに Arduino でコードを書きます。バグになりやすい箇所・処理の抜け・型の問題はありますか？」

**AIの回答（要約）：**
- 温度/湿度はfloat型のため、7セグ表示前に整数化のルールを固定した方がよい。
- DHT11の読み取り失敗時（NaN）の分岐がないと誤判定の原因になる。
- 閾値境界（20度, 28度）は状態が揺れやすいのでヒステリシスを推奨。

**対応した内容：**
- displayTemperature()で整数化・表示範囲クリップを明記した。
- readTemperature()に失敗時のsensorError処理を追加した。
- updateState()にヒステリシス条件を追加した。

---

### Q2: 単体テスト仕様の確認

> 「Section 5 の単体テスト仕様書で、各関数の動作が正しく検証できていますか？テストが不足している項目や、境界値テストが必要な箇所を教えてください。」

**AIの回答（要約）：**
- 入力系は正常値/異常値の両方を確認できるため妥当。
- 出力系は高温・低温・監視中の3状態を全てテスト対象にすると網羅性が上がる。
- 境界値（20, 21, 27, 28）のテストがあると状態遷移バグを防ぎやすい。

**対応した内容：**
- updateState()の単体テストに温度19/24/29を追加した。
- 出力系テストをupdateLED()/updateBuzzer()に合わせて更新した。
- 実装時に境界値テスト（20, 21, 27, 28）を追加予定とした。

---

## 7. グループレビュー記録

### 7-1. 指摘一覧

| No | 指摘内容 | 指摘者 | 対応 |
|:---|:---|:---|:---|
| 1 |  |  |  |
| 2 |  |  |  |
| 3 |  |  |  |

### 7-2. レビューを受けて変更した点

-
-

---

*初版: 2026-05-25 / AIレビュー: 2026-05-25 / グループレビュー後更新: YYYY-MM-DD*