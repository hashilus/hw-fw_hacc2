## HACC2 Python Simulator

- UDPポート: 5555（ハード側の`UDP_PORT`互換）
- GUI: SSR(4ch)とRGB(4個)を可視化

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
- config: `config`/`config ssrlink ...`/`config rgb0|rgb100`/`config trans`/`config random rgb`/`config ssr_freq`/`config save|load`
- その他: `info`/`sofia`/`mist`/`air`/`zerox`

注意: 設定の永続化は未実装（ダミー応答）。


