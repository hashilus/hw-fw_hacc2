## HACC2 Python Simulator

- UDPポート: 5555（ハード側の`UDP_PORT`互換）
- シリアル相当: GUI内のConsoleで同じコマンドを入力可能
- GUI: SSR(4ch)とRGB(4個)、WS2812(3系統の平均色)を可視化

### 起動

```bash
python app.py
```

PowerShell:
```powershell
python .\app.py
```

### 使い方

- Consoleに`help`でコマンド一覧
- UDPクライアントからも同一コマンドを送信可能（例: `echo -n "set 1,100" | nc -u 127.0.0.1 5555`）

### 実装状況

- SSR: `set`/`freq`/`get`
- RGB: `rgb`/`rgbget`
- WS2812: `ws2812`/`ws2812get`/`ws2812sys`/`ws2812off`
- config: `config`/`config ssrlink ...`/`config rgb0|rgb100`/`config trans`/`config random rgb`/`config ssr_freq`/`config save|load`
- その他: `info`/`sofia`/`mist`/`air`/`zerox`

注意: 設定の永続化は未実装（ダミー応答）。


