# HACC2 - Hashilus Air Control Controller 2

## 概要
HACC2は、Hashilusの空気制御システムのコントローラーです。Mbed OSを使用して実装されており、UDP経由でコマンドを受け付け、SSRとRGB LEDを制御します。

## バージョン
- 現在のバージョン: 2.1.0
- 主な変更点:
  - ゼロクロス検出機能の追加（P3_9ピン、両エッジ検出）
  - トライアック制御機能の実装（1サイクル内でのON時間可変制御）
  - 商用電源周波数の自動検出（50Hz/60Hz対応）
  - SSR制御の高精度化（ゼロクロス同期制御）
  - 設定色読み取りコマンドの追加（config rgb0 status, config rgb100 status）
  - enablePWMメソッドの削除（非推奨機能の整理）
  - SSR-LED連動機能の完全実装
  - ステータスLEDの動作仕様を追加
  - ネットワーク接続の安定性向上
  - パケット処理の最適化
  - バッファサイズの拡大（8KB）
  - パフォーマンス監視機能の追加
  - バージョン情報の一元管理

## ハードウェア要件
- Mbed対応ボード
- イーサネットインターフェース
- SSRドライバー（4チャンネル）
- RGB LEDドライバー（3チャンネル）

## ソフトウェア要件
- Mbed OS 6.x
- コンパイラ: ARM GCC

## ビルド方法
```bash
mbed compile -m <TARGET> -t GCC_ARM
```

## 使用方法

### UDPコマンド

UDPコマンドは、デバイスのIPアドレスとポート（デフォルト: 5000）に送信します。

#### 基本コマンド

- `help` - コマンド一覧を表示
- `info` - システム情報を表示
- `config` - 現在の設定を表示
- `config load` - 設定を読み込む
- `config save` - 設定を保存

#### SSR制御

- `set <channel> <duty>` - SSRのデューティ比を設定（0-100%）
- `get <channel>` - SSRのデューティ比を取得
- `freq <channel> <freq>` - SSRの周波数を設定（1-100Hz）

#### RGB LED制御

- `rgb <led_id> <r>,<g>,<b>` - RGB LEDの色を設定（0-255）
- `rgbget <led_id>` - RGB LEDの色を取得

#### SSR-LED連動設定

- `config ssrlink <on/off>` - SSR-LED連動の有効/無効を設定
- `config ssrlink status` - SSR-LED連動の状態を表示
- `config rgb0 <led_id>,<r>,<g>,<b>` - LEDの0%時の色を設定（led_id: 1-4）
- `config rgb0 status <led_id>` - LEDの0%時の色を読み取り（led_id: 1-4）
- `config rgb100 <led_id>,<r>,<g>,<b>` - LEDの100%時の色を設定（led_id: 1-3）
- `config rgb100 status <led_id>` - LEDの100%時の色を読み取り（led_id: 1-3）
- `config trans <ms>` - トランジション時間を設定（100-10000ms）

#### デバッグ

- `debug level <0-3>` - デバッグレベルを設定
- `debug status` - 現在のデバッグレベルを表示

### シリアルコマンド

シリアルコマンドは、115200bpsで送信します。改行コードはLF。

## UDPコマンド仕様
### 基本形式
- コマンドはカンマ区切りの形式で送信
- 応答は`OK`または`ERROR`で終了
- エラー時は`コマンド名,ERROR`の形式で返信

### 基本コマンド
#### デバイス情報取得
- コマンド: `info`
- 応答: `info,<デバイス名>,2.0.0,OK`
- 例: `info` → `info,HACC2,2.0.0,OK`

### SSR制御
#### 出力制御
- コマンド: `set <id>,<value>`
  - id: 0-4 (0は全チャンネル)
  - value: 0-100 (0=OFF, 100=ON)
  - 応答: `set <id>,<value>,OK`
- エイリアス: `ssr <id>,<value>`
- 特殊値:
  - `ON`: 100%出力
  - `OFF`: 0%出力
- 例:
  - `set 1,50` → `set 1,50,OK` (チャンネル1を50%で制御)
  - `set 0,ON` → `set 0,100,OK` (全チャンネルを100%で制御)
  - `ssr 2,OFF` → `ssr 2,0,OK` (チャンネル2をOFF)

#### PWM周波数設定
- コマンド: `freq <id>,<value>`
  - id: 0-4 (0は全チャンネル)
  - value: 1-10 (Hz)
  - 応答: `freq <id>,<value>,OK`
- 例: `freq 0,5` → `freq 0,5,OK` (全チャンネルを5Hzに設定)

#### 状態取得
- コマンド: `get <id>`
  - id: 1-4
  - 応答: `get <id>,<duty>,<freq>,OK`
- 例: `get 1` → `get 1,50,5,OK` (チャンネル1の状態: デューティ比50%, 周波数5Hz)

### RGB LED制御
#### 色設定
- コマンド: `rgb <id>,<r>,<g>,<b>`
  - id: 0-4 (0は全LED)
  - r,g,b: 0-255
  - 応答: `rgb <id>,<r>,<g>,<b>,OK`
- 例:
  - `rgb 1,255,0,0` → `rgb 1,255,0,0,OK` (LED1を赤色に設定)
  - `rgb 0,0,255,0` → `rgb 0,0,255,0,OK` (全LEDを緑色に設定)

#### 状態取得
- コマンド: `rgbget <id>`
  - id: 1-4
  - 応答: `rgbget <id>,<r>,<g>,<b>,OK`
- 例: `rgbget 1` → `rgbget 1,255,0,0,OK` (LED1の現在の色: 赤)

### 設定コマンド
#### SSR-LED連動設定
- コマンド: `config ssrlink <on/off>`
  - 応答: `SSR-LED link enabled/disabled`
- 例: `config ssrlink on` → `SSR-LED link enabled`

- コマンド: `config ssrlink status`
  - 応答: `SSR-LED link is enabled/disabled`
- 例: `config ssrlink status` → `SSR-LED link is enabled`

#### 設定色の設定
- コマンド: `config rgb0 <led_id>,<r>,<g>,<b>`
  - led_id: 1-4
  - r,g,b: 0-255
  - 応答: `LED<id> 0% color set to R:<r> G:<g> B:<b>`
- 例: `config rgb0 1,0,0,0` → `LED1 0% color set to R:0 G:0 B:0`

- コマンド: `config rgb100 <led_id>,<r>,<g>,<b>`
  - led_id: 1-3
  - r,g,b: 0-255
  - 応答: `LED<id> 100% color set to R:<r> G:<g> B:<b>`
- 例: `config rgb100 1,255,255,255` → `LED1 100% color set to R:255 G:255 B:255`

#### 設定色の読み取り
- コマンド: `config rgb0 status <led_id>`
  - led_id: 1-4
  - 応答: `LED<id> 0% color: R:<r> G:<g> B:<b>`
- 例: `config rgb0 status 1` → `LED1 0% color: R:0 G:0 B:0`

- コマンド: `config rgb100 status <led_id>`
  - led_id: 1-3
  - 応答: `LED<id> 100% color: R:<r> G:<g> B:<b>`
- 例: `config rgb100 status 1` → `LED1 100% color: R:255 G:255 B:255`

#### トランジション時間設定
- コマンド: `config trans <ms>`
  - ms: 100-10000 (ミリ秒)
  - 応答: `Transition time set to <ms> ms`
- 例: `config trans 1000` → `Transition time set to 1000 ms`

### 特殊コマンド
#### ミスト制御
- コマンド: `mist <duration>`
  - duration: 0-10000 (ミリ秒)
  - 応答: `mist <duration>,OK`
- 動作: スプレーを指定時間噴射する
- 例: `mist 1000` → `mist 1000,OK` (1秒間ミストを制御)

#### ゼロクロス検出状態確認
- コマンド: `zerox`
- 応答: `zerox,<status>,<interval>,<count>,<frequency>,OK`
  - status: DETECTED/NOT_DETECTED
  - interval: 最後のゼロクロス間隔（マイクロ秒）
  - count: 検出回数
  - frequency: 計算された周波数（Hz）
- 例: `zerox` → `zerox,DETECTED,8333,1200,120.0,OK`

#### エアー制御
- コマンド: `air <level>`
  - level: 0-2
    - 0: エアーOFF
    - 1: エアーLOW
    - 2: エアーHIGH
  - 応答: `air <level>,OK`
- 例: `air 2` → `air 2,OK` (エアーガンの出力をHIGHにする)

#### かわいいコマンド
- コマンド: `sofia`
- 応答: `sofia,KAWAII,OK` (ソフィアはかわいい、いいね？)

## パフォーマンス監視
- パケット処理時間の統計情報
  - 平均処理時間
  - 最大処理時間
  - 処理パケット数
- 100パケットごとに統計情報を表示
- 処理時間が100msを超える場合に警告を表示

## ゼロクロス検出・トライアック制御機能

### ゼロクロス検出
- **検出ピン**: P3_9（両エッジ検出）
- **検出方式**: 立ち上がり・立ち下がりエッジの両方を検出
- **周波数測定**: 商用電源周波数（50Hz/60Hz）を自動検出
- **精度**: マイクロ秒単位での高精度測定

### トライアック制御
- **制御方式**: ゼロクロス同期制御
- **ON時間**: 1msec固定（高精度タイマー制御）
- **デューティ比**: 0-100%の範囲で1%単位制御
- **応答性**: 次のゼロクロスで即座に反映

### 動作原理
1. **ゼロクロス検出**: P3_9ピンで商用電源のゼロクロス点を検出
2. **周波数測定**: 立ち上がりエッジ間隔から電源周波数を計算
3. **タイミング計算**: デューティ比に応じたONタイミングを計算
4. **トライアック制御**: 計算されたタイミングでSSRをON/OFF制御

### 制御精度
- **50Hz地域**: 10msec周期での精密制御
- **60Hz地域**: 8.33msec周期での精密制御
- **デューティ比変換**: 0-99%を0-80%に変換（100%は変換なし）

## SSR-LED連動機能

SSR-LED連動機能により、SSR1のデューティ比に応じてRGB LEDの色が自動的に変化します。

### 動作原理
- SSR1のデューティ比（0-100%）を監視
- 0%時の色と100%時の色の間を線形補間
- 設定されたトランジション時間で滑らかに色を変化
- 独立したスレッドで100Hzで更新

### 設定項目
- **SSR-LED連動**: 有効/無効の切り替え
- **0%時の色**: SSR出力0%時のRGB LEDの色（LED1-4）
- **100%時の色**: SSR出力100%時のRGB LEDの色（LED1-3）
- **トランジション時間**: 色変化にかける時間（100-10000ms）

### 使用例
```
# SSR-LED連動を有効化
config ssrlink on

# LED1の0%時の色を黒に設定
config rgb0 1,0,0,0

# LED1の100%時の色を白に設定
config rgb100 1,255,255,255

# トランジション時間を1秒に設定
config trans 1000

# SSR1を50%で制御（LED1は灰色に変化）
set 1,50
```

## エラー処理
- ネットワーク切断時の自動再接続
- ソケットエラー時の自動再初期化
- パケット処理時間の監視と警告
- エラー発生時のログ出力
  - レベル0: エラーのみ
  - レベル1: 基本情報
  - レベル2: 詳細情報

## 技術的な変更点

### 削除された機能
- `enablePWM`メソッド: 非推奨機能として削除（何もしないメソッドだったため）

### 追加された機能
- **ゼロクロス検出機能**: P3_9ピンでの両エッジ検出
- **トライアック制御**: ゼロクロス同期での高精度制御
- **電源周波数自動検出**: 50Hz/60Hz地域の自動判定
- **ゼロクロス状態確認コマンド**: `zerox`コマンド
- 設定色読み取りコマンド: `config rgb0 status`, `config rgb100 status`
- SSR-LED連動機能の完全実装
- トランジション制御の改善

### パフォーマンス改善
- 不要なメソッド呼び出しの削除
- コードの最適化と整理
- SSR制御の高精度化（ゼロクロス同期）
- 割り込み優先度の最適化

### ハードウェア要件の変更
- **ゼロクロス検出回路**: P3_9ピンへの接続が必要
- **高精度タイマー**: マイクロ秒単位での制御
- **割り込み処理**: 最高優先度でのゼロクロス検出

## ライセンス
- プロプライエタリ

## 作者
- Hashilus 

## シリアルコンソール
### 接続方法
- ボーレート: 115200 bps
- データビット: 8
- パリティ: なし
- ストップビット: 1
- フロー制御: なし
- 改行コード LF

### コマンド一覧
#### 基本コマンド
- `help`: ヘルプを表示
- `info`: デバイス情報を表示
- `reboot`: デバイスを再起動
- `debug level <0-3>`: デバッグレベルを設定
- `debug status`: 現在のデバッグレベルを表示

#### SSR制御
- `set <id> <value>`: SSRの出力を設定
  - id: 1-4
  - value: 0-100 (0=OFF, 100=ON)
  - 例: `set 1 50` (チャンネル1を50%で制御)
- `freq <id> <value>`: PWM周波数を設定
  - id: 1-4
  - value: 1-10 (Hz)
  - 例: `freq 1 5` (チャンネル1を5Hzに設定)
- `get <id>`: SSRの状態を取得
  - id: 1-4
  - 例: `get 1` (チャンネル1の状態を表示)

#### RGB LED制御
- `rgb <id> <r> <g> <b>`: RGB LEDの色を設定
  - id: 1-4
  - r,g,b: 0-255
  - 例: `rgb 1 255 0 0` (LED1を赤色に設定)
- `rgbget <id>`: RGB LEDの状態を取得
  - id: 1-4
  - 例: `rgbget 1` (LED1の現在の色を表示)

#### 設定コマンド
- `config`: 現在の設定を表示
- `config save`: 設定を保存
- `config load`: 設定を読み込み
- `config ssrlink on/off`: SSR-LED連動の有効/無効
- `netbios <name>`: NETBIOS名を設定
- `ip <address>`: IPアドレスを設定
- `mask <netmask>`: サブネットマスクを設定
- `gateway <address>`: デフォルトゲートウェイを設定
- `dhcp on/off`: DHCPの有効/無効

### コマンド入力機能
- コマンド履歴: 上下矢印キーで履歴を参照
- カーソル移動: 左右矢印キーでカーソルを移動
- バックスペース: 文字を削除
- タブ補完: サポート予定

### 応答形式
- 成功時: コマンドの実行結果を表示
- エラー時: エラーメッセージを表示
- デバッグ情報: デバッグレベルに応じて詳細情報を表示 

## ステータスLED
システムの状態を表示する3色LED（赤、緑、青）の動作仕様です。

### LEDの配置
- LED1: 赤色
- LED2: 緑色
- LED3: 青色

### 表示パターン
1. 初期化中（青色点滅）
   - 青色LEDが500ms周期で点滅
   - システム起動時に表示

2. 正常動作（緑色点灯）
   - 緑色LEDが常時点灯
   - 通常動作時の状態

3. エラー状態（赤色点滅）
   - 赤色LEDが1秒周期で点滅
   - エラー発生時に表示

4. パケット受信（紫色点灯）
   - 赤色と青色LEDが同時点灯
   - 一時的（200ms）に表示
   - UDPパケット受信時に表示

5. コマンド実行（オレンジ色点灯）
   - 赤色と緑色LEDが同時点灯
   - 一時的（500ms）に表示
   - コマンド実行時に表示

### 動作の特徴
- 独立したスレッドで動作（50ms間隔で更新）
- 一時的な状態は自動的に元の状態に戻る
- パケット受信とコマンド実行は優先度が高い状態を表示

